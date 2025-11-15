# librb

librb is a runtime support library used by FoxComet IRCd and derived from libratbox,
the common runtime support code originally developed for ircd-ratbox.

## History

librb has evolved significantly from its libratbox origins and is no longer
compatible with libratbox itself. It cannot be used as a drop-in replacement
for libratbox, which is why it was renamed to librb.

## Important Notes

1. **Thread Safety**: Most of this code is not threadsafe at this point. Do
   not expect thread-safe operations in this library.

2. **Line Buffer Size**: The linebuf code is designed to handle 512 bytes per
   line and nothing more. Anything beyond that length (unless in raw mode) gets
   discarded. For some non-IRC purposes, this can be a problem, but for IRCd
   purposes it's sufficient.

3. **Helper Communication**: The helper code when transmitting data between
   helpers has the same 512 byte limit, as it reuses the linebuf code.

For more information about librb's history and contributors, see the CREDITS
file in the FoxComet project root.
