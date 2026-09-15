import re
import requests

HEADERS = {"User-Agent": "Mozilla/5.0", "Accept": "application/json, text/plain, */*"}

BASE_URL = "https://ruz.spbstu.ru"

TEACHER_SEARCH_URL = f"{BASE_URL}/search/teacher"
GROUP_SEARCH_URL = f"{BASE_URL}/search/groups"

SCHEDULE_TEACHER_URL = f"{BASE_URL}/api/v1/ruz/teachers/{{id}}/scheduler"
SCHEDULE_GROUP_URL = f"{BASE_URL}/api/v1/ruz/scheduler/{{id}}"
SCHEDULE_ROOM_URL = (
    f"{BASE_URL}/api/v1/ruz/buildings/{{building_id}}/rooms/{{room_id}}/scheduler"
)
BUILDINGS_URL = f"{BASE_URL}/api/v1/ruz/buildings"
ROOMS_URL = f"{BASE_URL}/api/v1/ruz/buildings/{{building_id}}/rooms"


def _to_str_or_none(value):
    if value is None or value == "":
        return "None"
    return str(value)


def _safe_get_json(url, params=None):
    response = requests.get(url, headers=HEADERS, params=params, timeout=15)
    response.raise_for_status()
    return response.json()


def _safe_get_text(url, params=None):
    response = requests.get(url, headers=HEADERS, params=params, timeout=15)
    response.raise_for_status()
    return response.text


def _find_teacher_id_by_name(teacher_name):
    html = _safe_get_text(TEACHER_SEARCH_URL, params={"q": teacher_name})

    matches = re.findall(
        r'href=["\'](?:https://ruz\.spbstu\.ru)?/teachers/(\d+)["\']', html
    )
    if matches:
        return int(matches[0])

    matches = re.findall(r"/teachers/(\d+)", html)
    if matches:
        return int(matches[0])

    return None


def _find_group_id_by_name(group_name):
    html = _safe_get_text(GROUP_SEARCH_URL, params={"q": group_name})

    matches = re.findall(r"/faculty/\d+/groups/(\d+)", html)
    if matches:
        return int(matches[0])

    matches = re.findall(r"/groups/(\d+)", html)
    if matches:
        return int(matches[0])

    return None


def _find_room_ids(building_name, room_name):
    # Получаем список всех корпусов
    data = _safe_get_json(BUILDINGS_URL)
    buildings = data.get("buildings", [])

    building_id = None
    for b in buildings:
        if b.get("name") == building_name:
            building_id = b.get("id")
            break

    if building_id is None:
        return None

    # Получаем список аудиторий в этом корпусе
    data = _safe_get_json(ROOMS_URL.format(building_id=building_id))
    rooms = data.get("rooms", [])

    room_id = None
    for r in rooms:
        if r.get("name") == room_name:
            room_id = r.get("id")
            break

    if room_id is None:
        return None

    return building_id, room_id


def _extract_lessons_from_schedule(schedule_response, date):
    days = schedule_response.get("days", [])
    for day in days:
        if day.get("date") == date:
            return day.get("lessons", [])
    return []


def _extract_lesson_fields(lesson):
    start_time = lesson.get("time_start")
    end_time = lesson.get("time_end")
    subject = lesson.get("subject")

    lesson_type = None
    if isinstance(lesson.get("typeObj"), dict):
        lesson_type = lesson["typeObj"].get("name")

    teachers = lesson.get("teachers", [])
    teacher = None
    if teachers:
        teacher = ",".join(
            t.get("full_name", "None") for t in teachers if isinstance(t, dict)
        )

    groups = lesson.get("groups", [])
    group_names = None
    if groups:
        group_names = ",".join(
            g.get("name", "None") for g in groups if isinstance(g, dict)
        )

    auditories = lesson.get("auditories", [])
    place = None
    if auditories:
        place = ",".join(
            a.get("name", "None") for a in auditories if isinstance(a, dict)
        )

    return {
        "start_time": _to_str_or_none(start_time),
        "end_time": _to_str_or_none(end_time),
        "subject": _to_str_or_none(subject),
        "type": _to_str_or_none(lesson_type),
        "teacher": _to_str_or_none(teacher),
        "groups": _to_str_or_none(group_names),
        "place": _to_str_or_none(place),
    }


def _format_lessons(lessons):
    if not lessons:
        return None

    parts = []
    for lesson in lessons:
        item = _extract_lesson_fields(lesson)
        parts.append(
            f"Time:{item['start_time']}-{item['end_time']}\n"
            f"Subject:{item['subject']}\n"
            f"Type:{item['type']}\n"
            f"Teacher:{item['teacher']}\n"
            f"Groups:{item['groups']}\n"
            f"Place:{item['place']}\n"
        )
    return "".join(parts)


def get_teacher_schedule(teacher_name, date):
    teacher_id = _find_teacher_id_by_name(teacher_name)
    if teacher_id is None:
        return None

    schedule_response = _safe_get_json(
        SCHEDULE_TEACHER_URL.format(id=teacher_id),
        params={"date": date},
    )
    lessons = _extract_lessons_from_schedule(schedule_response, date)
    return _format_lessons(lessons)


def get_group_schedule(group_name, date):
    group_id = _find_group_id_by_name(group_name)
    if group_id is None:
        return None

    schedule_response = _safe_get_json(
        SCHEDULE_GROUP_URL.format(id=group_id),
        params={"date": date},
    )
    lessons = _extract_lessons_from_schedule(schedule_response, date)
    return _format_lessons(lessons)


def get_room_schedule(building_name, room_name, date):
    ids = _find_room_ids(building_name, room_name)
    if ids is None:
        return None

    building_id, room_id = ids

    schedule_response = _safe_get_json(
        SCHEDULE_ROOM_URL.format(
            building_id=building_id,
            room_id=room_id,
        ),
        params={"date": date},
    )
    lessons = _extract_lessons_from_schedule(schedule_response, date)
    return _format_lessons(lessons)


if __name__ == "__main__":
    print(get_teacher_schedule("Писков Александр Александрович", "2026-09-15"))
    print("-----")
    print(get_group_schedule("5151001/40202", "2026-09-15"))
    print("-----")
    print(get_room_schedule("Главное здание", "237", "2026-09-15"))
