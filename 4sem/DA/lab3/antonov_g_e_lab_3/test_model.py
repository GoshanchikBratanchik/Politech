# -*- coding: utf-8 -*-
import os
import sys

sys.stdout.reconfigure(encoding='utf-8')

from predict import predict

# Названия папок и соответствующие метки
FOLDER_LABELS = {
    "humans": "человек",
    "people": "человек",
    "horses": "животное",
    "animals": "животное",
    "cats": "животное",
    "dogs": "животное",
}


def test_folder(folder_path, expected_label, results, n):
    if len(results) >= n:
        return
    images = [f for f in os.listdir(folder_path) if f.lower().endswith(('.jpg', '.jpeg', '.png'))]
    for img in images:
        if len(results) >= n:
            break
        img_path = os.path.join(folder_path, img)
        try:
            result = predict(image_path=img_path, weights_path="resnet18_binary.pth")
            correct = result.lower() == expected_label.lower()
            results.append((img, result, expected_label, correct))
        except Exception as e:
            results.append((img, f"ОШИБКА: {e}", expected_label, False))


def run_tests(dataset_dir, title, n=10):
    results = []

    if not os.path.exists(dataset_dir):
        return

    for folder in os.listdir(dataset_dir):
        folder_path = os.path.join(dataset_dir, folder)
        if not os.path.isdir(folder_path):
            continue
        expected_label = FOLDER_LABELS.get(folder.lower())
        if expected_label is None:
            print(f"  [!] Папка '{folder}' не распознана, пропускаю")
            continue
        test_folder(folder_path, expected_label, results, n)

    if not results:
        print(f"Картинки не найдены в {dataset_dir}")
        return

    print("=" * 70)
    print(f"{title}: {len(results)} тестов")
    print("=" * 70)

    correct_count = 0
    for i, (img, returned, expected, correct) in enumerate(results, 1):
        status = "правильно" if correct else "НЕВЕРНО"
        if correct:
            correct_count += 1
        print(f"{i:02d}. {img:<30} вернуло: {returned:<12} ожидалось: {expected:<12} {status}")

    accuracy = correct_count / len(results) * 100
    print(f"Итог: {correct_count}/{len(results)} правильно | Точность: {accuracy:.2f}%")
    print()


if __name__ == "__main__":
    run_tests("dataset/train", "ИЗ ОБУЧАЮЩЕЙ ВЫБОРКИ", n=10)
    run_tests("dataset/val", "ИЗ ВАЛИДАЦИОННОЙ ВЫБОРКИ", n=10)
    if os.path.exists("test_custom"):
        run_tests("test_custom", "ИЗ ПРОИЗВОЛЬНОГО ИСТОЧНИКА", n=10)