TEST_PROFILES := sleep.out
BIN = ../xrprof
RSCRIPT = Rscript

all: $(TEST_PROFILES)

clean:
	$(RM) $(TEST_PROFILES)

%.out: %.R
	echo $(BIN)
	sudo BIN=$(BIN) RSCRIPT=$(RSCRIPT) ./harness.sh $<

.PHONY: all clean
