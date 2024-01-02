import subprocess

def send_email():
    file = open("email.txt", "w")

    # 写入内容到文件
    file.write("Hello, world!")

    # 关闭文件
    file.close()

    curl_command = 'curl --url "smtps://smtp.163.com:465" --ssl-reqd \
                    --mail-from "15889671017@163.com" --mail-rcpt "77391656@qq.com" \
                    --user "15889671017@163.com:jason1982ID" --insecure \
                    --upload-file email.txt'

    try:
        subprocess.run(curl_command, shell=True, check=True)
        print("Email sent successfully.")
    except subprocess.CalledProcessError as e:
        print("Failed to send email:", e)

if __name__ == '__main__':
    send_email()