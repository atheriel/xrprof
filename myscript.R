library(tidyverse)
library(nycflights13)

flights_wider <- flights

for (i in 1:10000) {
  message("Iteration: ", i)

  flights_longer <-
    flights_wider %>%
    pivot_longer(c(origin, dest), names_to = "relation")

  flights_wider <-
    flights_longer %>%
    pivot_wider(names_from = relation)
}
