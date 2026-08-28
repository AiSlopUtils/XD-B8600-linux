# Local libexword changes

Baseline: <https://github.com/brijohn/libexword>, branch `2.0-dev`, commit
`d186e35`.

This copy adds two small portability changes used for the XD-B8600 work:

- Undefine a platform `ntohll` macro before defining libexword's function.
- Honor `EXWORD_DATA_DIR` so the models/data directory can be selected without
  installing it globally.

The macOS test binary was compiled with signed `char`; this avoids corrupting
the binary OBEX protocol stream on platforms where plain `char` is signed.
The source remains GPL-2.0-or-later under the upstream notices.
