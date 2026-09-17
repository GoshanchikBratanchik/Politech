import os, random, shutil
from pathlib import Path

random.seed(42)


def collect_images(folder, prefix=None):
    exts = (".jpg", ".jpeg", ".png")
    files = []
    for root, _, names in os.walk(folder):
        for n in names:
            if n.lower().endswith(exts):
                if prefix is None or n.lower().startswith(prefix):
                    files.append(os.path.join(root, n))
    return files


def split_and_copy(files, out_train, out_val, val_ratio=0.2):
    random.shuffle(files)
    n_val = int(len(files) * val_ratio)
    val_files, train_files = files[:n_val], files[n_val:]
    os.makedirs(out_train, exist_ok=True)
    os.makedirs(out_val, exist_ok=True)
    for i, f in enumerate(train_files):
        shutil.copy2(f, os.path.join(out_train, f"{i}{Path(f).suffix}"))
    for i, f in enumerate(val_files):
        shutil.copy2(f, os.path.join(out_val, f"{i}{Path(f).suffix}"))
    print(f"  train: {len(train_files)}, val: {len(val_files)}")


# --- Кошки (только файлы "cat.*.jpg", собак игнорируем) ---
cat_files = collect_images("raw_data/cats_dogs/cats_dogs_light", prefix="cat.")
print(f"Найдено кошек: {len(cat_files)}")
split_and_copy(cat_files, "dataset/train/animals", "dataset/val/animals")

# --- Люди (папка "1" = снимки с людьми) ---
human_files = collect_images("raw_data/humans/human detection dataset/1")
print(f"Найдено людей: {len(human_files)}")
random.shuffle(human_files)
human_files = human_files[: len(cat_files)]  # балансируем под число кошек
print(f"Используем людей (для баланса): {len(human_files)}")
split_and_copy(human_files, "dataset/train/humans", "dataset/val/humans")

print("\nГотово. Структура:")
os.system("find dataset -maxdepth 2 -type d | sort")
print("\nКоличество файлов по папкам:")
os.system(
    'find dataset -maxdepth 2 -type d -exec sh -c \'echo -n "{}: "; ls "{}" | wc -l\' \\;'
)
