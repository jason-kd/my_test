import subprocess
import sys
import os

REPO_OWNER = os.getenv('REPO_OWNER')
REPO_NAME = os.getenv('REPO_NAME')
PR_NUMBER = os.getenv('PR_NUMBER')

print(f"REPO_OWNER ID is {REPO_OWNER}")
print(f"REPO_NAME is {REPO_NAME}")
print(f"PR_NUMBER is {PR_NUMBER}")
