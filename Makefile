SHELL = /bin/bash

.PHONY: help hello dos daemon vectors e2e clean

help:
	@echo "SerialDFS build targets:"
	@echo "  hello    Build dos/build/HELLO.EXE (smoke pin)"
	@echo "  dos      Build all DOS .EXE targets"
	@echo "  daemon   Run Python daemon (requires --serial, --baud, --root args)"
	@echo "  vectors  Generate protocol golden test vectors"
	@echo "  e2e      Run end-to-end test suite"
	@echo "  clean    Remove build artifacts"

hello:
	cd dos && wmake hello

dos:
	cd dos && wmake all

daemon:
	python3 -m linux.serdfsd $(ARGS)

vectors:
	python3 protocol/crc16_reference.py --gen-vectors protocol/vectors/

e2e:
	@for s in tests/e2e/*.sh; do bash "$$s" || exit 1; done

clean:
	$(MAKE) -C dos clean
	find linux -name '__pycache__' -exec rm -rf {} + 2>/dev/null; true
