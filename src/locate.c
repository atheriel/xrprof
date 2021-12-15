#include <stdio.h>      /* for fprintf */
#include "locate.h"
#include "memory.h"

#ifdef __linux
#include <fcntl.h>      /* for open */
#include <stddef.h>     /* for ptrdiff_t */
#include <stdlib.h>     /* for malloc */
#include <string.h>     /* for strstr, strndup */

#include <elf.h>
#include <libelf.h>
#include <gelf.h>

#define MAX_LIBR_PATH_LEN 128

static int find_libR(pid_t pid, char **path, uintptr_t *addr) {
  char maps_file[32];
  snprintf(maps_file, sizeof(maps_file), "/proc/%d/maps", pid);
  FILE *file = fopen(maps_file, "r");
  if (!file) {
    char msg[51]; // 19 for the message + 32 for the buffer above.
    snprintf(msg, 51, "error: Cannot open %s", maps_file);
    perror(msg);
    return -1;
  }
  *path = NULL;

  char buffer[1024];
  uintptr_t start = 0;
  while (fgets(buffer, sizeof(buffer), file)) {
    if (!start) {
      /* Extract the process's own code address. */
      start = (uintptr_t) strtoul(buffer, NULL, 16);
    }
    if (strstr(buffer, "libR.so")) {
      /* Extract the address. */
      *addr = (uintptr_t) strtoul(buffer, NULL, 16);

      /* Prefix the path with the process's view of the filesystem, which might
         be affected by a namespace (as in the case of a container). */
      *path = calloc(MAX_LIBR_PATH_LEN, 1);
      snprintf(*path, MAX_LIBR_PATH_LEN, "/proc/%d/root%s", pid,
               strstr(buffer, "/"));

      /* Remove the trailing '\n'. */
      char *linebreak = strstr(*path, "\n");
      if (linebreak) {
        *linebreak = '\0';
      }

      break;
    }
  }

  fclose(file);

  /* Either (1) this R program does not use libR.so, or (2) it's not actually an
     R program. */
  if (!*path) {
    *addr = start;
    return -1;
  }
  return 0;
}


int locate_libR_globals(phandle pid, struct libR_globals *out) {
  /* Open the same libR.so in the tracer so we can determine the symbol offsets
     to read memory at in the tracee. */

  if (elf_version(EV_CURRENT) == EV_NONE) {
    fprintf(stderr, "error: Can't set the ELF version. %s\n",
            elf_errmsg(elf_errno()));
    return -1;
  }

  char *path = NULL;
  uintptr_t remote = 0;
  if (find_libR(pid, &path, &remote) < 0) {
    /* Try finding the symbols in the executable directly. */
    path = calloc(MAX_LIBR_PATH_LEN, 1);
    snprintf(path, MAX_LIBR_PATH_LEN, "/proc/%d/exe", pid);
  }

  /* if (verbose) fprintf(stderr, "Found %s at %p in pid %d.\n", path, */
  /*                      (void *) addr, pid); */

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    char msg[64];
    snprintf(msg, 64, "error: Cannot open %s", path);
    perror(msg);
    free(path);
    return -1;
  }

  Elf *elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
  if (elf == NULL) {
    fprintf(stderr, "error: %s is not a valid ELF file. %s\n", path,
            elf_errmsg(elf_errno()));
    close(fd);
    free(path);
    return -1;
  }

  /* TODO: 32-bit support? */
  Elf64_Ehdr *ehdr = elf64_getehdr(elf);
  if (!ehdr) {
    fprintf(stderr, "error: %s is not a valid 64-bit ELF file. %s\n", path,
            elf_errmsg(elf_errno()));
    elf_end(elf);
    close(fd);
    free(path);
    return -1;
  }

  Elf64_Shdr shdr;
  Elf_Scn *scn = NULL;
  while ((scn = elf_nextscn(elf, scn)) != NULL) {
    gelf_getshdr(scn, &shdr);
    if (shdr.sh_type == SHT_DYNSYM) {
      break;
    }
  }
  if (!scn) {
    fprintf(stderr, "error: Can't find the symbol table in %s.\n", path);
    elf_end(elf);
    close(fd);
    free(path);
    return -1;
  }

  Elf_Data *data = elf_getdata(scn, NULL);
  Elf64_Sym sym;
  char *symbol;
  uintptr_t value;
  ssize_t bytes;
  for (int i = 0; i < shdr.sh_size / shdr.sh_entsize; i++) {
    gelf_getsym(data, i, &sym);
    symbol = elf_strptr(elf, shdr.sh_link, sym.st_name);

    if (strncmp("R_GlobalContext", symbol, 15) == 0) {
      /* The R_GlobalContext value will change, so we only want the address to
         read the value from. */
      out->context_addr = remote + sym.st_value;
    } else if (strncmp("R_DoubleColonSymbol", symbol, 19) == 0) {
      /* copy_address() will print its own errors. */
      bytes = copy_address(pid, (void *)remote + sym.st_value, &value,
                           sizeof(uintptr_t));
      out->doublecolon = bytes < sizeof(uintptr_t) ? 0 : value;
    } else if (strncmp("R_TripleColonSymbol", symbol, 19) == 0) {
      bytes = copy_address(pid, (void *)remote + sym.st_value, &value,
                           sizeof(uintptr_t));
      out->triplecolon = bytes < sizeof(uintptr_t) ? 0 : value;
    } else if (strncmp("R_DollarSymbol", symbol, 14) == 0) {
      bytes = copy_address(pid, (void *)remote + sym.st_value, &value,
                           sizeof(uintptr_t));
      out->dollar = bytes < sizeof(uintptr_t) ? 0 : value;
    } else if (strncmp("R_BracketSymbol", symbol, 15) == 0) {
      bytes = copy_address(pid, (void *)remote + sym.st_value, &value,
                           sizeof(uintptr_t));
      out->bracket = bytes < sizeof(uintptr_t) ? 0 : value;
    }
  }

  elf_end(elf);
  close(fd);
  free(path);

  if (!out->doublecolon || !out->triplecolon || !out->dollar || !out->bracket ||
      !out->context_addr) {
    fprintf(stderr, "error: Failed to locate required R global variables in process %d's memory. Are you sure it is an R program?\n",
            pid);
    return -1;
  }

  return 0;
}
#elif defined(__WIN32)
#include <windows.h>
#include <psapi.h> /* for EnumProcessModules */
#include <dbghelp.h> /* for SymInitialize, SymLoadModuleEx, etc */

int locate_libR_globals(phandle pid, struct libR_globals *out) {
  if (proc_suspend(pid) < 0) {
    return -1;
  }

  /* TODO: Should we use TRUE here to force loading symbols from all modules? */
  if (!SymInitialize(pid, NULL, FALSE)) {
    fprintf(stderr, "error: Failed to load remote process symbols: %ld.\n",
            GetLastError());
    return -1;
  }

  HMODULE mods[1024];
  DWORD mod_bytes;
  if (!EnumProcessModules(pid, mods, sizeof(mods), &mod_bytes)) {
    fprintf(stderr, "error: Failed to enumerate remote process modules: %ld.\n",
            GetLastError());
    goto error;
  }
  int entries = mod_bytes / sizeof(HMODULE);

  TCHAR mpath[256];
  DWORD64 base;
  for (int i = 0; i < entries; i++ ) {
    if (!GetModuleFileNameEx(pid, mods[i], mpath, sizeof(mpath) / sizeof(TCHAR))) {
      fprintf(stderr, "error: Failed to get remote process module: %ld.\n",
              GetLastError());
      goto error;
    }

    /* A module that looks like R. */
    if (!strstr(mpath, "R.dll")) {
      continue;
    }

    base = SymLoadModuleEx(pid, NULL, mpath, NULL, (DWORD64) mods[i], 0, NULL, 0);
    if (!base) {
      fprintf(stderr, "error: Failed to load symbols for %s (0x%p): %ld.\n",
              mpath, mods[i], GetLastError());
      goto error;
    }

    uintptr_t value;
    ssize_t bytes;
    /* This is actually the crazy structure SymFromName uses. */
    struct {
      SYMBOL_INFO info;
      char buf[MAX_SYM_NAME];
    } info;
    info.info.SizeOfStruct = sizeof(SYMBOL_INFO);
    info.info.ModBase = base;
    info.info.MaxNameLen = MAX_SYM_NAME - 1;
    char *sym;

    sym = "R_GlobalContext";
    if (!SymFromName(pid, sym, &info.info)) {
      if (GetLastError() != 123) {
        fprintf(stderr, "error: Failed to lookup symbol: %ld.\n", GetLastError());
        goto error;
      }
    } else {
      out->context_addr = info.info.Address;
    }

    sym = "R_DoubleColonSymbol";
    if (!SymFromName(pid, sym, &info.info)) {
      if (GetLastError() != 123) {
        fprintf(stderr, "error: Failed to lookup symbol: %ld.\n", GetLastError());
        goto error;
      }
    } else {
      /* copy_address() will print its own errors. */
      bytes = copy_address(pid, (void *) info.info.Address, &value,
                           sizeof(uintptr_t));
      out->doublecolon = bytes < sizeof(uintptr_t) ? 0 : value;
    }

    sym = "R_TripleColonSymbol";
    if (!SymFromName(pid, sym, &info.info)) {
      if (GetLastError() != 123) {
        fprintf(stderr, "error: Failed to lookup symbol: %ld.\n", GetLastError());
        goto error;
      }
    } else {
      bytes = copy_address(pid, (void *) info.info.Address, &value,
                           sizeof(uintptr_t));
      out->triplecolon = bytes < sizeof(uintptr_t) ? 0 : value;
    }

    sym = "R_DollarSymbol";
    if (!SymFromName(pid, sym, &info.info)) {
      if (GetLastError() != 123) {
        fprintf(stderr, "error: Failed to lookup symbol: %ld.\n", GetLastError());
        goto error;
      }
    } else {
      bytes = copy_address(pid, (void *) info.info.Address, &value,
                           sizeof(uintptr_t));
      out->dollar = bytes < sizeof(uintptr_t) ? 0 : value;
    }

    sym = "R_BracketSymbol";
    if (!SymFromName(pid, sym, &info.info)) {
      if (GetLastError() != 123) {
        fprintf(stderr, "error: Failed to lookup symbol: %ld.\n", GetLastError());
        goto error;
      }
    } else {
      bytes = copy_address(pid, (void *) info.info.Address, &value,
                           sizeof(uintptr_t));
      out->bracket = bytes < sizeof(uintptr_t) ? 0 : value;
    }

    if (!SymUnloadModule64(pid, base)) {
      fprintf(stderr, "error: Failed to unload symbols for %s (0x%p): %ld.\n",
              mpath, mods[i], GetLastError());
      goto error;
    }
  }

  if (!out->doublecolon || !out->triplecolon || !out->dollar || !out->bracket ||
      !out->context_addr) {
    fprintf(stderr, "error: Failed to locate required R global variables in \
remote process's memory. Are you sure it is an R program?\n");
    goto error;
  }

  SymCleanup(pid);
  return proc_resume(pid);

 error:
  SymCleanup(pid);
  proc_resume(pid);
  return -1;
}
#elif defined(__MACH__) // macOS support.
#include <mach/mach_vm.h>       /* for mach_vm_read_overwrite */
#include <mach-o/dyld_images.h> /* for dyld_all_image_infos */
#include <mach-o/loader.h>      /* for MH_EXECUTE, MH_DYLIB */
#include <mach-o/nlist.h>       /* for nlist */
#include <sys/param.h>          /* for MAXPATHLEN */
#include <stdlib.h>             /* for calloc, free */

static int find_libR(phandle p, char *path, uintptr_t *addr) {
  kern_return_t ret;
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  task_dyld_info_data_t info = {0};
  ret = task_info(p.task, TASK_DYLD_INFO, (task_info_t) &info, &count);
  if (ret != KERN_SUCCESS) {
    fprintf(stderr, "error: Failed to query remote task info: %s (%d).\n",
            mach_error_string(ret), ret);
    return -1;
  }

  struct dyld_all_image_infos images_info = {0};
  if (copy_address(p, (void *) info.all_image_info_addr, (void *) &images_info,
                   sizeof(images_info)) < 0) {
    return -1;
  }

  struct dyld_image_info images[images_info.infoArrayCount];
  if (copy_address(p, (void *) images_info.infoArray, (void *) &images,
                   images_info.infoArrayCount * sizeof(struct dyld_image_info)) < 0) {
    return -1;
  }

  size_t bytes;
  char fname[MAXPATHLEN + 1];
  for (int i = 0; i < images_info.infoArrayCount; i++) {
    if (copy_address(p, (void *) images[i].imageFilePath, (void *) &fname,
                     MAXPATHLEN) < 0) {
      return -1;
    }
    bytes = strnlen(fname, MAXPATHLEN);
    if (bytes <= 0) {
      continue;
    }
    /* To catch cases where we're not linked against libR, default to the first
       entry (which is the executable itself). */
    if (i == 0 || strstr(fname, "libR")) {
      *addr = (uintptr_t) images[i].imageLoadAddress;
      memcpy(path, fname, bytes);
      path[bytes] = '\0';
      if (i != 0) {
        /* We found libR. */
        return 0;
      }
    }
  }

  return 0;
}

int locate_libR_globals(phandle p, struct libR_globals *out) {
  char path[MAXPATHLEN + 1];
  uintptr_t remote = 0;
  if (find_libR(p, path, &remote) < 0) {
    return -1;
  }

  /* TODO: mmap() this into memory instead of reading the whole thing into a
     buffer. */
  FILE *file = fopen(path, "rb");
  if (!file) {
    char msg[MAXPATHLEN];
    snprintf(msg, MAXPATHLEN, "error: Cannot open %s", path);
    perror(msg);
    return -1;
  }
  fseek(file, 0L, SEEK_END);
  size_t bytes = ftell(file);
  char *buffer = calloc(bytes, 1);
  if (!buffer) {
    return -1;
  }
  fseek(file, 0L, SEEK_SET);
  fread(buffer, 1, bytes, file);
  fclose(file);

  struct mach_header_64 *hdr = (struct mach_header_64 *) buffer;
  if (hdr->magic != MH_MAGIC_64) {
    fprintf(stderr, "error: Unsupported Mach-O magic number: 0x%x.\n",
            hdr->magic);
    return -1;
  }
  if (hdr->filetype != MH_EXECUTE && hdr->filetype != MH_DYLIB) {
    fprintf(stderr, "error: Unexpected Mach-O header filetype: 0x%x.\n",
            hdr->filetype);
    return -1;
  }

  /* Loop over the segments to find the symbol table. */

  uintptr_t offset = sizeof(struct mach_header_64);
  uintptr_t strtab_offset = 0;
  struct segment_command_64 *seg;
  for (int i = 0; i < hdr->ncmds; i++) {
    seg = (struct segment_command_64 *) (buffer + offset);
    /* Handle cases where the base address might be moved. */
    if (seg->cmd == LC_SEGMENT_64) {
      if (strstr(seg->segname, "__TEXT")) {
        strtab_offset -= seg->vmaddr;
      }
      offset += seg->cmdsize;
      continue;
    }
    if (seg->cmd != LC_SYMTAB) {
      offset += seg->cmdsize;
      continue;
    }

    /* We've found the symbol table, which records offsets to an array of
       symbols entries (as nlist_64 structures) as well as the a blob of
       null-terminated strings, including the actual human-readable symbols. */

    struct symtab_command *symtab = (struct symtab_command *) seg;
    struct nlist_64 *symbols = (struct nlist_64 *) (buffer + symtab->symoff);
    char* strings = (char *) (buffer + strtab_offset + symtab->stroff);
    char *symbol;
    uintptr_t value;
    for (int j = 0; j < symtab->nsyms; j++) {
      /* Skip symbols equivalent to the empty string (which seems to happen
         sometimes) and symbols with no corresponding address. These are
         irrelevant. */
      if (symbols[j].n_un.n_strx == 0 || symbols[j].n_value == 0) {
        continue;
      }
      /* Avoid reading random, potentially-non-string memory. This should never
         happen (tm). */
      if (symbols[j].n_un.n_strx > symtab->strsize) {
        continue;
      }
      symbol = strings + symbols[j].n_un.n_strx;
      /* In Mach-O binaries, symbols are prefixed with an underscore vis a vis
         their C equivalents. */
      if (strncmp("_R_GlobalContext", symbol, 16) == 0) {
        out->context_addr = remote + symbols[j].n_value;
      } else if (strncmp("_R_DoubleColonSymbol", symbol, 20) == 0) {
        bytes = copy_address(p, (void *) remote + symbols[j].n_value, &value,
                             sizeof(uintptr_t));
        out->doublecolon = bytes < sizeof(uintptr_t) ? 0 : value;
      } else if (strncmp("_R_TripleColonSymbol", symbol, 20) == 0) {
        bytes = copy_address(p, (void *) remote + symbols[j].n_value, &value,
                             sizeof(uintptr_t));
        out->triplecolon = bytes < sizeof(uintptr_t) ? 0 : value;
      } else if (strncmp("_R_DollarSymbol", symbol, 15) == 0) {
        bytes = copy_address(p, (void *) remote + symbols[j].n_value, &value,
                             sizeof(uintptr_t));
        out->dollar = bytes < sizeof(uintptr_t) ? 0 : value;
      } else if (strncmp("_R_BracketSymbol", symbol, 16) == 0) {
        bytes = copy_address(p, (void *) remote + symbols[j].n_value, &value,
                             sizeof(uintptr_t));
        out->bracket = bytes < sizeof(uintptr_t) ? 0 : value;
      }
    }

    break;
  }

  if (!out->doublecolon || !out->triplecolon || !out->dollar || !out->bracket ||
      !out->context_addr) {
    fprintf(stderr, "error: Failed to locate required R global variables in "
            "process %d's memory. Are you sure it is an R program?\n", p.pid);
    return -1;
  }

  free(buffer);
  return 0;
}
#else
#error "No support for this platform."
#endif
