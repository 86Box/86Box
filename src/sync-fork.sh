#!/bin/bash
set -e

git fetch upstream
git checkout no-vmm
git rebase upstream/main

if [ $? -eq 0 ]; then
  git push origin no-vmm --force-with-lease
  echo "✅ no-vmm synced and patches reapplied!"
else
  echo "❌ Conflicts detected. Resolve manually, then run:"
  echo "   git rebase --continue && git push origin no-vmm --force-with-lease"
fi