---
name: reference-workflow-push-credential
description: git's keychain token cannot push .github/workflows changes; push those with the gh credential helper
metadata:
  type: reference
---

Plain `git push` uses the macOS keychain token, which lacks the `workflow` scope, so GitHub refuses any push that changes `.github/workflows/*` ("refusing to allow an OAuth App to create or update workflow"). The `gh` login has `workflow`. Push such branches with:

    GIT_CONFIG_COUNT=2 GIT_CONFIG_KEY_0=credential.helper GIT_CONFIG_VALUE_0= \
      GIT_CONFIG_KEY_1=credential.helper GIT_CONFIG_VALUE_1='!gh auth git-credential' git push ...

`gh pr view` can report the old head for a few seconds after the push; check `git ls-remote` instead. See [[feedback-merge-policy]].
