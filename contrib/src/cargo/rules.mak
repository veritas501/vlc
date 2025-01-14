# cargo/cargo-c installation via rustup

RUST_VERSION=1.47.0
CARGOC_VERSION=0.6.13
RUSTUP_VERSION=1.22.1
RUSTUP_URL=https://github.com/rust-lang/rustup/archive/$(RUSTUP_VERSION).tar.gz

RUSTUP = . $(CARGO_HOME)/env && \
	RUSTUP_DIST_SERVER=https://rsproxy.cn RUSTUP_UPDATE_ROOT=https://rsproxy.cn/rustup \
	RUSTUP_HOME=$(RUSTUP_HOME) CARGO_HOME=$(CARGO_HOME) rustup

$(TARBALLS)/rustup-$(RUSTUP_VERSION).tar.gz:
	$(call download_pkg,$(RUSTUP_URL),rustup)

.sum-cargo: rustup-$(RUSTUP_VERSION).tar.gz

rustup: rustup-$(RUSTUP_VERSION).tar.gz .sum-cargo
	$(UNPACK)
	$(MOVE)

# Test if we can use the host libssl library
ifeq ($(shell unset PKG_CONFIG_LIBDIR PKG_CONFIG_PATH; \
	pkg-config "openssl >= 1.0.1" 2>/dev/null || \
	pkg-config "libssl >= 2.5" 2>/dev/null || echo FAIL),)
CARGOC_FEATURES=
else
# Otherwise, let cargo build and statically link its own openssl
CARGOC_FEATURES=--features=cargo/vendored-openssl
endif

# When needed (when we have a Rust dependency not using cargo-c), the cargo-c
# installation should go in a different package
.cargo: rustup
	@mkdir -p $(CARGO_HOME)
	@echo "[source.crates-io]" > $(CARGO_HOME)/config
	@echo "replace-with = 'rsproxy'" >> $(CARGO_HOME)/config
	@echo "" >> $(CARGO_HOME)/config
	@echo "[source.rsproxy]" >> $(CARGO_HOME)/config
	@echo "registry = \"https://rsproxy.cn/crates.io-index\"" >> $(CARGO_HOME)/config
	@echo "" >> $(CARGO_HOME)/config
	@echo "[source.rsproxy-sparse]" >> $(CARGO_HOME)/config
	@echo "registry = \"sparse+https://rsproxy.cn/index/\"" >> $(CARGO_HOME)/config
	@echo "" >> $(CARGO_HOME)/config
	@echo "[registries.rsproxy]" >> $(CARGO_HOME)/config
	@echo "index = \"https://rsproxy.cn/crates.io-index\"" >> $(CARGO_HOME)/config
	@echo "" >> $(CARGO_HOME)/config
	@echo "[net]" >> $(CARGO_HOME)/config
	@echo "git-fetch-with-cli = true" >> $(CARGO_HOME)/config
	@echo "update cargo mirror"
	cd $< && curl --proto '=https' --tlsv1.2 -sSf https://rsproxy.cn/rustup-init.sh -o ./rustup-init.sh \
         && chmod +x ./rustup-init.sh \
	 && RUSTUP_INIT_SKIP_PATH_CHECK=yes \
	 RUSTUP_HOME=$(RUSTUP_HOME) CARGO_HOME=$(CARGO_HOME) \
	 RUSTUP_DIST_SERVER=https://rsproxy.cn RUSTUP_UPDATE_ROOT=https://rsproxy.cn/rustup \
	 ./rustup-init.sh -y --default-toolchain $(RUST_VERSION)
	$(RUSTUP) default $(RUST_VERSION)
	$(RUSTUP) target add $(RUST_TARGET)
	unset PKG_CONFIG_LIBDIR PKG_CONFIG_PATH CFLAGS CPPFLAGS LDFLAGS; \
		$(CARGO) install --locked $(CARGOC_FEATURES) cargo-c  --version $(CARGOC_VERSION)
	touch $@
