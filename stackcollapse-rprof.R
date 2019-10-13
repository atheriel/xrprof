#!/usr/bin/Rscript

usage <- function() {
  cat("Usage: stackcollapse-rout.R [-h|--help] <Rprof.out>\n")
}

argv <- commandArgs(TRUE)
if (length(argv) == 0) {
  # Attempt to read from stdin.
  infile <- file("stdin", blocking = FALSE)
} else if (length(argv) != 1) {
  usage()
  quit(status = 1L)
} else if (argv[1] %in% c("-h", "--help")) {
  usage()
  quit(status = 0L)
} else {
  infile <- file(argv[1])
  open(infile, open = "rt")
}

chunk <- readLines(infile)
if (length(chunk) == 0) {
  usage()
  quit(status = 1L)
}

chunk <- chunk[-1] # For now, ignore the header.

# Line profiling support.
src_file_lines <- grepl("^#File", chunk)
if (any(src_file_lines)) {
  src_files <- gsub("^#File [0-9]+: (.*)$", "\\1", chunk[src_file_lines])
  exist <- file.exists(src_files)
  src_files[!exist] <- gsub("^.*/([^/]+)/R/(.*)$", "\\1:\\2", src_files[!exist])

  # Remove the Srcref annotations.
  chunk <- chunk[!src_file_lines]

  # Omit srcrefs at the start of lines; we can't really handle them.
  chunk <- gsub("^[0-9]+#[0-9]+\\s", "", chunk)

  chunk <- strsplit(chunk, "\\s\"")
} else {
  chunk <- strsplit(chunk, " ")
}

process_line <- function(line) {
  # Annotate functions in a way FlameGraph can understand.
  line <- gsub("<Native:([^;]+)>", "\\1_[n]", line)
  line <- gsub("<Built-in:([^;]+)>", "\\1_[i]", line)

  srcrefs <- grepl("[0-9]+#[0-9]+", line)
  linenos <- gsub(".*\\s[0-9]+#([0-9]+)", "\\1", line[srcrefs])
  filenos <- as.integer(gsub(".*\\s([0-9]+)#[0-9]+", "\\1", line[srcrefs]))
  files <- src_files[filenos]
  srcref_fmt <- paste(files, linenos, sep = ":")

  line <- gsub("\\s[0-9]+#[0-9]+", "", line)
  line <- gsub("[\" ]", "", line)
  line[srcrefs] <- paste(line[srcrefs], "at", srcref_fmt)

  line
}

chunk <- lapply(chunk, process_line)

# Collapse stack listings.
#
# NOTE: This is a pretty inelegant approach.

stacks <- list(chunk[[1]])
counts <- 1L

for (i in seq_along(chunk)[-1]) {
  index <- length(counts)
  stack <- chunk[[i]]
  if (i == 1 || !identical(stack, chunk[[i - 1]])) {
    stacks[[index + 1]] <- stack
    counts <- c(counts, 1L)
  } else {
    counts[index] <- counts[index] + 1L
  }
}

stopifnot(length(counts) == length(stacks)) # This should never happen.

# Convert to the FlameGraph format.

formatted <- vapply(stacks, function(stack) {
  paste(rev(stack), collapse = ";")
}, character(1))
formatted <- gsub("\"", "", formatted)
formatted <- paste(formatted, counts)

writeLines(formatted, con = stdout())
