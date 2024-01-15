import requests
import time
import os
import sys

# GitHub API相关参数
api_url = 'https://api.github.com'
repo_owner = os.environ['REPO_OWNER']
repo_name = os.environ['REPO_NAME']
pr_number = os.environ['PR_NUMBER']  # PR号码
access_token = os.environ['ACCESS_TOKEN']

print(f"Repository owner: {repo_owner}")
print(f"Repository name: {repo_name}")
print(f"pr_number: {pr_number}")

# 认证信息（如果需要）
headers = {
    'Authorization': f'Bearer {access_token}'
}

while True:
    # 获取PR的评论
    comments_url = f'{api_url}/repos/{repo_owner}/{repo_name}/pulls/{pr_number}/comments'
    # comments_url = f'{api_url}/repos/{repo_owner}/{repo_name}/pulls/{pr_number}/reviews'
    response = requests.get(comments_url, headers=headers)
    comments = response.json()  

        
    # 检查评论是否有已解决标记
    for comment in comments:
       print(f"Comment by {comment['user']['login']}: {comment['body']} : review id: {comment['pull_request_review_id']} ")
          
       sys.stdout.flush()  # 刷新标准输出缓冲区
        
       #if comment['pull_request_review_id'] is not None and comment['state'] == 'RESOLVED':
            # 执行你想要的操作或调用工作流程
           #print(f"Resolved comment found: {comment['body']}")
           #sys.stdout.flush()  # 刷新标准输出缓冲区
            # 退出程序
           #break

    # 添加适当的延迟，避免频繁请求
    time.sleep(5)  # 延迟2秒后再次检测
    print(f"check count")

# 程序执行到这里时会退出
