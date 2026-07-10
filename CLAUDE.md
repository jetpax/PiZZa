# PiZZa — instructions for Claude sessions

## Never commit planning documents (this repo is PUBLIC)

Work orders, handovers, milestone plans, scope docs, and session notes are
private and must never be committed or pushed. The gitignore covers
`HANDOVER*`, `*WORK-ORDER*`, `*WORK_ORDER*`, `WO-*.md`, `*_PLAN.md`,
`*_SCOPE.md`, and `notes/frames/` — do not force-add anything it catches.

- Local sessions: private notes live in `~/github/SS/notes/`, outside this
  repo.
- Cloud/remote sessions: deliver plans and work orders in your final
  response text, never as files in the repo. Commit only product code and
  user-facing docs.

## Repo shape

- `main` is the trunk. `dev` mirrors `main` and exists because the Arduino
  Boards-Manager manifest URL is frozen at
  `raw.githubusercontent.com/jetpax/PiZZa/dev/os/Arduino/package_pizza_index.json`
  — keep `dev` fast-forwarded to `main`, never diverge it.
- `apps/<Name>` are Zephyr applications; `apps/lib/` holds shared libs
  (sdl2shim, btinput) consumed via CMake `include()`.
- The card-side PINN layout is still `os/<NAME>` — `stage-os.sh` maps repo
  `apps/<NAME>` to card `os/<NAME>`. Do not "fix" the `os/` strings in
  `stage-os.sh`.
- `os/Arduino/package_pizza_index.json` is a frozen public endpoint; the
  single canonical copy lives at that path deliberately (no duplicate in
  `apps/`).

## Conventions

- Git identity: `jetpax <jetpax@users.noreply.github.com>`. Terse
  subject-only commit messages. No `Co-Authored-By` trailers.
- Vendor/copyrighted binaries (`.hcd` patchram, ROMs, disk images) are never
  committed; each `firmware/` dir carries a `PROVENANCE.md` with source URLs
  and sha256 to re-fetch.
- Commit only after runtime verification on hardware (the user flashes and
  reports results).
