import qrcode
import cv2
import numpy as np
from PIL import Image

# 1. Generate the Wi-Fi QR Code
wifi_string = "WIFI:T:WPA;S:Singer5g;P:israel1948;;"
qr = qrcode.QRCode(version=1, box_size=10, border=2)
qr.add_data(wifi_string)
qr.make(fit=True)
qr_img = qr.make_image(fill_color="black", back_color="white").convert('RGB')

# 2. Generate the ArUco Marker (Using the 4x4 dictionary, ID: 22)
aruco_dict = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50)
aruco_img = np.zeros((200, 200), dtype=np.uint8)
cv2.aruco.generateImageMarker(aruco_dict, 22, 200, aruco_img, 1)

# Convert ArUco to an RGB PIL Image
aruco_pil = Image.fromarray(aruco_img).convert('RGB')

# 3. Paste the QR code in the center of the ArUco marker
# Calculate coordinates to center the smaller QR code inside the larger ArUco marker
aruco_size = 300 # Resizing the ArUco canvas
aruco_img_resized = aruco_pil.resize((aruco_size, aruco_size))

qr_size = 120 # Desired size of the QR code
qr_img_resized = qr_img.resize((qr_size, qr_size))

offset = (aruco_size - qr_size) // 2
aruco_img_resized.paste(qr_img_resized, (offset, offset))

# Save the final combined image
aruco_img_resized.save("wifi_aruco_marker.png")
print("Combined marker saved as wifi_aruco_marker.png")

