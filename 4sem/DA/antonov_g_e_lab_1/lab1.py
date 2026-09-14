import argparse
import os
import shutil


def validate_path(path):
    if not isinstance(path, str):
        raise ValueError("Path must be a string")
    if path is None:
        raise ValueError("Path cannot be None")
    if path.strip() == "":
        raise ValueError("Path cannot be empty or whitespace")


def f_create(path):
    try:
        validate_path(path)
        dir_name = os.path.dirname(path)
        if dir_name:
            os.makedirs(dir_name, exist_ok=True)
        with open(path, 'w', encoding='utf-8'):
            pass
        print(f"[+] File created successfully: {path}")

    except Exception as e:
        print(f"[-] Failed to create file '{path}'. {e}")
        raise


def f_delete(path):
    try:
        validate_path(path)

        if not os.path.exists(path):
            raise FileNotFoundError(f"File '{path}' does not exist")

        if os.path.isdir(path):
            raise IsADirectoryError(f"Path '{path}' is a directory")

        os.remove(path)

        print(f"[+] File deleted successfully: {path}")

    except Exception as e:
        print(f"[-] Failed to delete file '{path}'. {e}")
        raise


def f_write(path, content):
    try:
        validate_path(path)

        if content is None:
            raise ValueError("Content cannot be None")

        if os.path.isdir(path):
            raise IsADirectoryError(f"Path '{path}' is a directory")

        dir_name = os.path.dirname(path)
        if dir_name:
            os.makedirs(dir_name, exist_ok=True)

        with open(path, 'w', encoding='utf-8') as f:
            f.write(content)

        print(f"[+] Content written successfully to: {path}")

    except Exception as e:
        print(f"[-] Failed to write to file '{path}'. {e}")
        raise


def f_read(path):
    try:
        validate_path(path)

        if not os.path.exists(path):
            raise FileNotFoundError(f"File '{path}' does not exist")

        if os.path.isdir(path):
            raise IsADirectoryError(f"Path '{path}' is a directory")

        with open(path, 'r', encoding='utf-8') as f:
            content = f.read()

        print(f"[+] File read successfully: {path}")
        return content

    except Exception as e:
        print(f"[-] Failed to read file '{path}'. {e}")
        raise


def f_copy(src, dest):
    try:
        validate_path(src)
        validate_path(dest)

        if not os.path.exists(src):
            raise FileNotFoundError(f"Source '{src}' does not exist")

        if os.path.isdir(src):
            raise IsADirectoryError(f"Source '{src}' is a directory")
        if os.path.exists(dest) and os.path.isdir(dest):
            raise IsADirectoryError(f"Destination '{dest}' is a directory")

        dir_name = os.path.dirname(dest)
        if dir_name:
            os.makedirs(dir_name, exist_ok=True)

        shutil.copy2(src, dest)

        print(f"[+] File copied successfully from '{src}' to '{dest}'")

    except Exception as e:
        print(f"[-] Failed to copy file from '{src}' to '{dest}'. {e}")
        raise

def f_rename(src, dest):
    try:
        validate_path(src)
        validate_path(dest)

        if not os.path.exists(src):
            raise FileNotFoundError(f"Source '{src}' does not exist")

        if os.path.isdir(src):
            raise IsADirectoryError(f"Source '{src}' is a directory")

        dir_name = os.path.dirname(dest)
        if dir_name:
            os.makedirs(dir_name, exist_ok=True)

        os.rename(src, dest)

        print(f"[+] File renamed successfully from '{src}' to '{dest}'")

    except Exception as e:
        print(f"[-] Failed to rename file from '{src}' to '{dest}'. {e}")
        raise


def main():
    ZOV = argparse.ArgumentParser()
    ZOV.add_argument('command')
    ZOV.add_argument('--path')
    ZOV.add_argument('--src')
    ZOV.add_argument('--dest')
    ZOV.add_argument('--content')
    args = ZOV.parse_args()
    if args.command == "create":
        f_create(args.path)
    elif args.command == "delete":
        f_delete(args.path)
    elif args.command == "write":
        f_write(args.path, args.content)
    elif args.command == "read":
        f_read(args.path)
    elif args.command == "copy":
        f_copy(args.src, args.dest)
    elif args.command == "rename":
        f_rename(args.src, args.dest)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()