import cv2
import numpy as np
import time

print(cv2.__version__)
img_num = 0
cam_num = 1

cam = cv2.VideoCapture(cam_num)

while True:
    ret_val, img = cam.read()
    cv2.imshow('cam: `{cam_num}, `{img_num++}``', img)
    time.sleep(5)
    if cv2.waitKey(1) == 27:
        break
cam.release()
cv2.destroyAllWindows()
