import os
import json
import logging
import tempfile

import telebot
from telebot import TeleBot
from telebot.types import Update
from flask import Flask, Response, request

# ─────────────────────────────────────────
# Настройки — замени на свои значения
# ─────────────────────────────────────────
TOKEN = "8909924340:AAFCk1Xr8zEGPx2Cem0D9na3ewFuyzdHKh0"          # токен от @BotFather
WEBHOOK_URL = "https://fresh-memes-deny.loca.lt"  # URL из ngrok/pinggy
PORT = 5000

# ─────────────────────────────────────────
# Логирование
# ─────────────────────────────────────────
logging.basicConfig(
    format="%(asctime)s - %(levelname)s - %(message)s",
    level=logging.INFO,
)
logger = logging.getLogger(__name__)

# ─────────────────────────────────────────
# Хранилище пользователей (в памяти)
# Структура: { user_id: { "password": str, "authenticated": bool, "awaiting_photo": bool } }
# ─────────────────────────────────────────
user_storage: dict = {}


# ─────────────────────────────────────────
# Импорт predict из лабы 3
# ─────────────────────────────────────────
def classify_image(file_bytes: bytes) -> str:
    """Сохраняет байты во временный файл и вызывает predict из лабы 3."""
    try:
        from predict import predict
        with tempfile.NamedTemporaryFile(suffix=".jpg", delete=False) as tmp:
            tmp.write(file_bytes)
            tmp_path = tmp.name
        result = predict(image_path=tmp_path, weights_path="resnet18_binary.pth")
        os.unlink(tmp_path)
        return result
    except Exception as e:
        logger.error("Ошибка классификации: %s", e)
        return "Ошибка классификации"


# ─────────────────────────────────────────
# Создание бота и регистрация обработчиков
# ─────────────────────────────────────────
bot = TeleBot(TOKEN)


@bot.message_handler(commands=["start", "help"])
def handle_start(message):
    text = (
        "Доступные команды:\n"
        "/register <пароль> — регистрация\n"
        "/login <пароль>    — вход\n"
        "/logout            — выход\n"
        "/predict           — классификация фото"
    )
    bot.send_message(message.chat.id, text)


@bot.message_handler(commands=["register"])
def handle_register(message):
    user_id = message.from_user.id
    args = message.text.split(maxsplit=1)

    if len(args) != 2:
        bot.send_message(message.chat.id, "Использование: /register <пароль>")
        return

    if user_id in user_storage:
        bot.send_message(message.chat.id, "Вы уже зарегистрированы")
        return

    password = args[1]
    user_storage[user_id] = {
        "password": password,
        "authenticated": False,
        "awaiting_photo": False,
    }
    bot.send_message(message.chat.id, "Регистрация успешна!")


@bot.message_handler(commands=["login"])
def handle_login(message):
    user_id = message.from_user.id
    args = message.text.split(maxsplit=1)

    if user_id not in user_storage:
        bot.send_message(message.chat.id, "Сначала зарегистрируйтесь командой /register")
        return

    if len(args) != 2:
        bot.send_message(message.chat.id, "Использование: /login <пароль>")
        return

    password = args[1]
    if user_storage[user_id]["password"] != password:
        bot.send_message(message.chat.id, "Неверный пароль")
        return

    user_storage[user_id]["authenticated"] = True
    bot.send_message(message.chat.id, "Вы успешно вошли!")


@bot.message_handler(commands=["logout"])
def handle_logout(message):
    user_id = message.from_user.id

    if user_id not in user_storage:
        bot.send_message(message.chat.id, "Вы не зарегистрированы")
        return

    user_storage[user_id]["authenticated"] = False
    user_storage[user_id]["awaiting_photo"] = False
    bot.send_message(message.chat.id, "Вы вышли из системы")


@bot.message_handler(commands=["predict"])
def handle_predict(message):
    user_id = message.from_user.id

    if user_id not in user_storage or not user_storage[user_id]["authenticated"]:
        bot.send_message(message.chat.id, "Вы не авторизованы. Используйте /login")
        return

    user_storage[user_id]["awaiting_photo"] = True
    bot.send_message(message.chat.id, "Отправьте фото для классификации")


@bot.message_handler(content_types=["photo"])
def handle_photo(message):
    user_id = message.from_user.id

    if user_id not in user_storage or not user_storage[user_id]["authenticated"]:
        bot.send_message(message.chat.id, "Вы не авторизованы. Используйте /login")
        return

    if not user_storage[user_id].get("awaiting_photo"):
        bot.send_message(message.chat.id, "Сначала отправьте команду /predict")
        return

    try:
        file_info = bot.get_file(message.photo[-1].file_id)
        file_bytes = bot.download_file(file_info.file_path)
        result = classify_image(file_bytes)
        bot.send_message(message.chat.id, f"Результат: {result}")
    except Exception as e:
        logger.error("Ошибка обработки фото: %s", e)
        bot.send_message(message.chat.id, "Ошибка при обработке изображения")
    finally:
        user_storage[user_id]["awaiting_photo"] = False


# ─────────────────────────────────────────
# Flask — принимает вебхуки от Telegram
# ─────────────────────────────────────────
app = Flask(__name__)


@app.route(f"/{TOKEN}", methods=["POST"])
def telegram_webhook():
    json_data = request.json
    if json_data:
        update = Update.de_json(json_data)
        bot.process_new_updates([update])
    return Response("OK")


@app.route("/health", methods=["GET"])
def healthcheck():
    return Response("OK")


# ─────────────────────────────────────────
# Запуск
# ─────────────────────────────────────────
if __name__ == "__main__":
    bot.remove_webhook()
    bot.set_webhook(url=f"{WEBHOOK_URL}/{TOKEN}")
    logger.info("Webhook установлен: %s/%s", WEBHOOK_URL, TOKEN)
    app.run(host="0.0.0.0", port=PORT)