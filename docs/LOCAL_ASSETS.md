# Local game assets

This repository does not distribute extracted Manhunt game data. To run the port, use files extracted from your own legally owned copy and place them in the paths expected by the Android loader.

The folders previously tracked under `app/src/main/assets/levels/` and `app/src/main/assets/export/`, plus `cash_pc.dff` and `cash_pc.txd`, are intentionally excluded from the current development branch. Git history is preserved; this change does not rewrite earlier commits.

The native loader currently expects its level/configuration files under `app/src/main/assets/export/ManHunt#pak/levels/` and other resources under `app/src/main/assets/levels/`. Restore the matching files locally before running the app. These local files are ignored by Git and will not be committed.
