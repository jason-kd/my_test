import subprocess
import sys

if len(sys.argv) < 4:
    args_count = len(sys.argv) - 1  # 减去脚本名称本身
    print("命令行参数个数:", args_count)
    print("Error: Insufficient command line arguments")
    print("Usage: python cross_commit_check.py REPO_OWNER REPO_NAME PR_NUMBER")
    sys.exit(1)

REPO_OWNER = sys.argv[1]
REPO_NAME = sys.argv[2]
PR_NUMBER = sys.argv[3]

print(f"REPO_OWNER ID is {REPO_OWNER}")
print(f"REPO_NAME is {REPO_NAME}")
print(f"PR_NUMBER is {PR_NUMBER}")
