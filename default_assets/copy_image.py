



import math
import shutil
import os

REGION_SIZE_PIXELS = 32*16*16 # one Region (32 chunks, of 16 blocks, of 16 pixels)

MAP_SIZE = 8*REGION_SIZE_PIXELS
IMAGE_SIZE = 256
IMAGE = r'256x256_test.png'
IMAGE_2 = r'256x256_test_alt.png'
FOLDER = r'test_images'

# clear folder
print("clearing directory...")
shutil.rmtree(FOLDER)
os.makedirs(FOLDER, exist_ok=True)


# MAP_SIZE / (IMAGE_SIZE * 2^x) = 1
# MAP_SIZE = (IMAGE_SIZE * 2^x)
# 2^x = MAP_SIZE / IMAGE_SIZE
# x = log2(MAP_SIZE / IMAGE_SIZE)
LEVELS = math.ceil(math.log(MAP_SIZE / IMAGE_SIZE) / math.log(2))

for level in range(0, LEVELS):
    pixels_accross = IMAGE_SIZE * 2**(LEVELS-level)
    images_across = math.ceil(pixels_accross / IMAGE_SIZE)
    print("level: ", level)
    print("pixels across: ", pixels_accross)
    print("images across: ", images_across)

    img_src = IMAGE if level % 2 == 0 else IMAGE_2

    # half = math.ceil(images_across/2)
    for x in range(0, images_across):
        for z in range(0, images_across):
            path = f"{FOLDER}/{level}/{x}"
            os.makedirs(path, exist_ok=True)
            new_filename = f'{-z}.png'
            new_file_path = os.path.join(path, new_filename)
            shutil.copy2(img_src, new_file_path)
        print(f"x: {x}")



