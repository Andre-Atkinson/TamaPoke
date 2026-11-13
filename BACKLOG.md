# Backlog

Findings deferred past the PR that found them (GitHub Issues are disabled on
this fork). One line each: what, why deferred, where it came from.

- **Settings screen power pills are unlabelled English codes** (`L10`/`L30`/`L60`/`L0`, `60s`/`30s`/`15s`/`--`, `icon`/`%`; only "off" is localised). The row isn't centred on CX, targets are 26 px tall, and the left pill touches the circle edge. Needs short localised labels or icons in all 8 languages. Deferred as a UI change outside the round-1 fixes. From the round-1 review of [#1](https://github.com/Andre-Atkinson/TamaPoke/pull/1) (L7).
- **Prompt for the time at boot when the soft clock was lost** (1.75C after a full power-off: the clock is reseeded from the last saved time, so bedtime now pauses until she sets it, but nothing asks her to). Owner chose the pause only; a boot prompt is a UI change. From the round-1 review of [#5](https://github.com/Andre-Atkinson/TamaPoke/pull/5) (L3).
