# Security Guide

## Sensitive Information

This project requires sensitive credentials that should **NEVER** be committed to the repository:

### Files with Sensitive Data (already in .gitignore)

- `sdkconfig` - Contains WiFi password, MQTT password
- `sdkconfig.old` - Backup of sdkconfig
- `.env` - Local environment variables
- `build/` - May contain artifacts with compiled credentials

### How to Configure Credentials Securely

#### Option 1: Menuconfig (ESP-IDF Standard)
```bash
idf.py menuconfig
```
Configure your credentials in "Component config" → "Project Configuration"

#### Option 2: Local File (Not committed)
```bash
cp sdkconfig.defaults sdkconfig.defaults.local
# Edit sdkconfig.defaults.local with your credentials
```

## Checklist Before Pushing

✅ **ALWAYS verify before git push:**

```bash
# 1. Check status
git status

# 2. Make sure these files DO NOT appear:
#    - sdkconfig
#    - sdkconfig.old
#    - .env
#    - files in build/

# 3. Verify what will be committed
git diff --cached

# 4. If you find credentials, remove immediately:
git reset HEAD <sensitive_file>
```

## What to Do If You Accidentally Commit Credentials

If you accidentally committed credentials:

### 1. If you have NOT pushed yet:
```bash
# Undo the last commit keeping the changes
git reset --soft HEAD~1

# Remove file from staging
git reset HEAD sdkconfig

# Make new commit without the sensitive file
git add .
git commit -m "Your commit without credentials"
```

### 2. If you have ALREADY pushed:

⚠️ **URGENT ACTION REQUIRED:**

1. **Change IMMEDIATELY all exposed credentials:**
   - WiFi password
   - MQTT password
   - Any other secrets

2. **Clean Git history:**
```bash
# Use git filter-branch or BFG Repo Cleaner
git filter-branch --force --index-filter \
  "git rm --cached --ignore-unmatch sdkconfig" \
  --prune-empty --tag-name-filter cat -- --all

# Force push (careful!)
git push origin --force --all
```

3. **Consider recreating the repository** if credentials are critical

## Best Practices

- ✅ Use strong and unique passwords
- ✅ Review the diff before each commit
- ✅ Configure Git to ignore sensitive files globally
- ✅ Use git hooks to prevent accidental commits
- ✅ Keep `.gitignore` updated
- ❌ Never commit configuration files with real credentials
- ❌ Never push `sdkconfig` or `sdkconfig.old`

## Git Hooks (Optional)

Create a pre-commit hook to automatically verify:

```bash
#!/bin/bash
# .git/hooks/pre-commit

if git diff --cached --name-only | grep -E "sdkconfig|\.env"; then
    echo "❌ ERROR: Attempt to commit sensitive file detected!"
    echo "Blocked files: sdkconfig, .env"
    exit 1
fi
```

Make the hook executable:
```bash
chmod +x .git/hooks/pre-commit
```

## Support

If you have questions about security or accidentally expose credentials, open an issue (without including the credentials!) or contact the maintainer.
