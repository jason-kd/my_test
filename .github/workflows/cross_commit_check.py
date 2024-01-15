import subprocess
import sys


REPO_OWNER = sys.argv[1]
REPO_NAME = sys.argv[2]
PR_NUMBER = sys.argv[3]

print(f"REPO_OWNER ID is {REPO_OWNER}")
print(f"REPO_NAME is {REPO_NAME}")
print(f"PR_NUMBER is {PR_NUMBER}")
