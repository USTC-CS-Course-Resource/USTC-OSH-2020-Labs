# 读取文件out.txt, 输出其前35行长度

f = open("out.txt", "r")

for i in range(35):
    recv = f.readline()
    if recv == None:
        break
    print("第{:2d}个长度为{}".format(i, len(recv)))

f.close()
