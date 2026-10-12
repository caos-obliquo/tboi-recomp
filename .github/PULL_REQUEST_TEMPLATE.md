## Summary

<!-- What this changes, and why. -->

Closes #

## Type of change

- [ ] Bug fix (non-breaking)
- [ ] New binding method or property
- [ ] New renderer backend
- [ ] Data or archive support
- [ ] Tooling or CI
- [ ] Documentation
- [ ] Refactor (no behaviour change)

## Clean-room attestation

- [ ] This change contains **no** Nicalis-owned content: no Lua sources, no
      sprites, audio, video, or game bytecode, no `resources/packed/*.a`
      archive or any extracted game data, no decompiled or copied Nicalis code.

## Verification performed

<!-- The commands you actually ran, and their results. Not a description of what
     you intended to run. -->

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

- [ ] Build is clean
- [ ] Tests pass
- [ ] Added or updated a test for this change
- [ ] `tools/gen-api-spec.py` output is unchanged, or is committed alongside

## Notes

Anything a reviewer should know that the diff does not show: findings, dead ends,
and what you deliberately left out.