import argparse

import torch
from PIL import Image
from torchvision import transforms

from model import create_model


CLASS_NAMES = ["животное", "человек"]


def predict(image_path, weights_path):
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    transform = transforms.Compose(
        [
            transforms.Resize((224, 224)),
            transforms.ToTensor(),
            transforms.Normalize(
                mean=[0.485, 0.456, 0.406],
                std=[0.229, 0.224, 0.225],
            ),
        ]
    )

    model = create_model(num_classes=2, pretrained=False)
    model.load_state_dict(torch.load(weights_path, map_location=device))
    model.to(device)
    model.eval()

    image = Image.open(image_path).convert("RGB")
    image = transform(image).unsqueeze(0).to(device)

    with torch.no_grad():
        outputs = model(image)
        predicted_class = outputs.argmax(dim=1).item()

    return CLASS_NAMES[predicted_class]


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True)
    parser.add_argument("--weights", default="resnet18_binary.pth")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    result = predict(image_path=args.image, weights_path=args.weights)
    print(f"Результат классификации: {result}")
