TEST_PROFILES := sleep.out
BIN = ../xrprof
RSCRIPT = Rscript
SUDO = sudo

all: $(TEST_PROFILES)

clean:
	$(RM) $(TEST_PROFILES)

%.out: %.R
	echo $(BIN)
	$(SUDO) BIN=$(BIN) ./harness.sh $<

.PHONY: all clean
