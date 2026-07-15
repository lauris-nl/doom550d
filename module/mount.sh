DEV=$(lsblk -rno PATH,LABEL | awk '$2=="EOS_DIGITAL"{print $1; exit}')
udisksctl mount -b "$DEV"
MNT=$(lsblk -rno MOUNTPOINT "$DEV" | head -n1)

echo "$MNT/ML/LOGS/DOOM550D.LOG"
