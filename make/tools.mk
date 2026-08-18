icons:
	@$(PYTHON) scripts/make-icons.py

quic-smoke:
	@$(RUNSH) scripts/quic-smoke.sh

.PHONY: icons quic-smoke
