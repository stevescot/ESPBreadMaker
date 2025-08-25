# Local Programs Directory

This directory provides a safe space to store and work with breadmaker program configurations without risking accidental overwrites of your device's custom programs.

## Purpose

- **Safe Storage**: Keep local copies of program files here without affecting git or uploads
- **Development**: Test and modify programs before uploading them to your device
- **Backup**: Store device program backups for reference or restoration

## Usage

### Backing Up Device Programs

To download your current device programs:

```powershell
# From the breadmaker_controller directory
curl "http://[device-ip]/api/programs" -o ../local_programs/device_programs_backup.json
curl "http://[device-ip]/api/programs_index" -o ../local_programs/device_programs_index_backup.json
```

### Uploading Programs to Device

1. **Test locally first**: Review your program files in this directory
2. **Copy to data directory**: Only when ready to upload
   ```powershell
   Copy-Item ../local_programs/my_programs.json data/programs.json
   ```
3. **Upload**: Use the normal file upload process
4. **Clean up**: Remove from data directory after upload
   ```powershell
   Remove-Item data/programs.json
   ```

## Safety Features

- ✅ This directory is **not uploaded** to the device by default
- ✅ Program files in `data/` directory are **gitignored** to prevent accidental commits
- ✅ Upload scripts don't automatically include program files
- ✅ You must manually copy files to trigger uploads

## File Types

Store these file types here safely:
- `programs.json` - Main program definitions
- `programs_index.json` - Program metadata and organization
- `program_*.json` - Individual program files
- `*_backup.json` - Device state backups

## Best Practices

1. **Always backup** device programs before making changes
2. **Test thoroughly** in this directory before uploading
3. **Use descriptive names** for backup files (include dates)
4. **Review changes** before copying to data directory
5. **Clean up** data directory after uploads to prevent future accidents

## Example Workflow

```powershell
# 1. Backup current device state
curl "http://192.168.1.100/api/programs" -o ../local_programs/backup_2024_01_15.json

# 2. Edit your programs in this directory
# (edit ../local_programs/my_new_programs.json)

# 3. When ready to upload
Copy-Item ../local_programs/my_new_programs.json data/programs.json

# 4. Upload via web interface or script
.\upload_files_robust.ps1

# 5. Clean up
Remove-Item data/programs.json
```

This workflow ensures your custom device programs are never accidentally overwritten!
