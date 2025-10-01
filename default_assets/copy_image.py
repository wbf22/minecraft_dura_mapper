



import math
import shutil
import os

# REGION_SIZE_PIXELS = 32*16*16 # one Region (32 chunks, of 16 blocks, of 16 pixels)

# MAP_SIZE = 32*REGION_SIZE_PIXELS
# IMAGE_SIZE = 8192
# IMAGE = r'8192x8192_test.png'
# IMAGE_2 = r'8192x8192_test_alt.png'
# FOLDER = r'test_images'

# # clear folder
# print("clearing directory...")
# shutil.rmtree(FOLDER)
# os.makedirs(FOLDER, exist_ok=True)


# # MAP_SIZE = 2**(2**levels)
# # log2(MAP_SIZE) = 2**levels
# # log2(log2(MAP_SIZE)) = levels
# LEVELS = math.floor(
#     math.log(math.log(MAP_SIZE) / math.log(2)) / math.log(2)
# )

# for level in range(0, LEVELS):
    
#     level_0_image_across = MAP_SIZE / IMAGE_SIZE;
#     level_0_images_per_current_level_image = 2**(2**level)
#     if level == 0:
#         level_0_images_per_current_level_image = 1
#     images_across = level_0_image_across / level_0_images_per_current_level_image

#     print("level: ", level)
#     print("images across: ", images_across)

#     img_src = IMAGE if level % 2 == 0 else IMAGE_2

#     half = math.ceil(images_across/2)
#     for x in range(-half, half):
#         for z in range(-half, half):
#             path = f"{FOLDER}/level_{level}"
#             os.makedirs(path, exist_ok=True)
#             new_filename = f'{x}_{z}.png'
#             new_file_path = os.path.join(path, new_filename)
#             shutil.copy2(img_src, new_file_path)
#         print(f"x: {x}")



FOLDER = r'test_images'

# clear folder
print("clearing directory...")
os.makedirs(FOLDER, exist_ok=True)
shutil.rmtree(FOLDER)

def copy_image(MAP_SIZE_IN_UNITS, IMAGE_SIZE_IN_UNITS, level, img_src):

    print("level: ", level)

    half = math.ceil(MAP_SIZE_IN_UNITS/2)
    for x in range(-half, half, IMAGE_SIZE_IN_UNITS):
        for z in range(-half, half, IMAGE_SIZE_IN_UNITS):
            path = f"{FOLDER}/level_{level}"
            os.makedirs(path, exist_ok=True)
            new_filename = f'{x}_{z}.png'
            new_file_path = os.path.join(path, new_filename)
            shutil.copy2(img_src, new_file_path)
        print(f"x: {x}")


REGION_SIZE_PIXELS = 32*16*16 # one Region (32 chunks, of 16 blocks, of 16 pixels)
MAP_SIZE = 32*REGION_SIZE_PIXELS

copy_image(MAP_SIZE, 8192, 0, 'pig_8192.png')
copy_image(MAP_SIZE, 8192*(2**(2**1)), 1, 'level_1.png')
copy_image(MAP_SIZE, 8192*(2**(2**2)), 2, 'level_2.png')
copy_image(MAP_SIZE, 8192*(2**(2**3)), 3, 'level_3.png')