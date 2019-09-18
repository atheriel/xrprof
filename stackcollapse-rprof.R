#!/usr/bin/Rscript

argv <- commandArgs(TRUE)
if (length(argv) != 1) {
  # Attempt to read from stdin.
  infile <- file("stdin", blocking = FALSE)
} else {
  infile <- file(argv[1])
  open(infile, open = "rt")
}

chunk <- readLines(infile)
if (length(chunk) == 0) {
  cat("Usage: stackcollapse-rout.R <Rprof.out>\n")
  quit(status = 1L)
}

chunk <- chunk[-1] # For now, ignore the header.

# Collapse stack listings.
#
# NOTE: This is a pretty inelegant approach.

chunk <- strsplit(chunk, " ")
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
