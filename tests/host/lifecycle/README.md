This harness compiles the real Application and WuxFlow sources against host
fakes, checking foreground release, deferred exits, the HOME callback, and
the flow's HOME gate and resource ownership. Each lifecycle scenario runs
in a separate process under AddressSanitizer and UndefinedBehaviorSanitizer.

Run `make -C tests/host/lifecycle` for the working tree. Run
`make -C tests/host/lifecycle old` to require the expected regression failures
on ccc8efd; `REF=<commit>` overrides the old source revision.

The ProcUI model encodes the documented wut contract. It proves the logic's
shape only, not console behaviour.

LeakSanitizer stops with a fatal error under ptrace (debuggers, some
sandboxes); run the targets from a normal shell.
