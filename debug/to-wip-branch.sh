cd ~/Projects/STM32/aio-node

wip_branch="wip/rs485-modbus-bringup-20261103"

git switch -c "$wip_branch"

git add -A
git add -f debug/

git diff --cached --quiet || git commit -m "WIP: RS-485 Modbus bring-up before rollback to v1.1"