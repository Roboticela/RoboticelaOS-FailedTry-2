#!/usr/bin/env python3
"""Write demo RoboMsg source catalogs for Goal 4.11 (run once / maintain)."""

from __future__ import annotations

import json
from pathlib import Path

HERE = Path(__file__).resolve().parent / "messages" / "demo"

# locale -> translations for shared message set
# Keys: greeting, login_title, login_failure, settings_lang, open_file, open_door,
#       items_one, items_other, welcome_emoji, combining

T = {
    "en": {
        "greeting": "Hello, {name}!",
        "login_title": "Sign in",
        "login_failure": "Sign-in failed",
        "settings_lang": "Language",
        "open_file": "Open",
        "open_door": "Open",
        "items_one": "{count} item",
        "items_other": "{count} items",
        "welcome_emoji": "Welcome 👋",
        "combining": "cafe\u0301",  # e + combining acute
    },
    "ur": {
        "greeting": "السلام علیکم، {name}!",
        "login_title": "سائن ان",
        "login_failure": "سائن ان ناکام",
        "settings_lang": "زبان",
        "open_file": "کھولیں",
        "open_door": "کھولیں",
        "items_one": "{count} آئٹم",
        "items_other": "{count} آئٹمز",
        "welcome_emoji": "خوش آمدید 👋",
        "combining": "cafe\u0301",
    },
    "ar": {
        "greeting": "مرحباً، {name}!",
        "login_title": "تسجيل الدخول",
        "login_failure": "فشل تسجيل الدخول",
        "settings_lang": "اللغة",
        "open_file": "فتح",
        "open_door": "فتح",
        "items_one": "{count} عنصر",
        "items_other": "{count} عناصر",
        "welcome_emoji": "أهلاً 👋",
        "combining": "cafe\u0301",
    },
    "fa": {
        "greeting": "سلام، {name}!",
        "login_title": "ورود",
        "login_failure": "ورود ناموفق",
        "settings_lang": "زبان",
        "open_file": "باز کردن",
        "open_door": "باز کردن",
        "items_one": "{count} مورد",
        "items_other": "{count} مورد",
        "welcome_emoji": "خوش آمدید 👋",
        "combining": "cafe\u0301",
    },
    "he": {
        "greeting": "שלום, {name}!",
        "login_title": "התחברות",
        "login_failure": "ההתחברות נכשלה",
        "settings_lang": "שפה",
        "open_file": "פתח",
        "open_door": "פתח",
        "items_one": "{count} פריט",
        "items_other": "{count} פריטים",
        "welcome_emoji": "ברוך הבא 👋",
        "combining": "cafe\u0301",
    },
    "hi": {
        "greeting": "नमस्ते, {name}!",
        "login_title": "साइन इन",
        "login_failure": "साइन इन विफल",
        "settings_lang": "भाषा",
        "open_file": "खोलें",
        "open_door": "खोलें",
        "items_one": "{count} वस्तु",
        "items_other": "{count} वस्तुएँ",
        "welcome_emoji": "स्वागत है 👋",
        "combining": "cafe\u0301",
    },
    "bn": {
        "greeting": "হ্যালো, {name}!",
        "login_title": "সাইন ইন",
        "login_failure": "সাইন ইন ব্যর্থ",
        "settings_lang": "ভাষা",
        "open_file": "খুলুন",
        "open_door": "খুলুন",
        "items_one": "{count} আইটেম",
        "items_other": "{count} আইটেম",
        "welcome_emoji": "স্বাগতম 👋",
        "combining": "cafe\u0301",
    },
    "th": {
        "greeting": "สวัสดี {name}!",
        "login_title": "เข้าสู่ระบบ",
        "login_failure": "เข้าสู่ระบบไม่สำเร็จ",
        "settings_lang": "ภาษา",
        "open_file": "เปิด",
        "open_door": "เปิด",
        "items_one": "{count} รายการ",
        "items_other": "{count} รายการ",
        "welcome_emoji": "ยินดีต้อนรับ 👋",
        "combining": "cafe\u0301",
    },
    "zh": {
        "greeting": "你好，{name}！",
        "login_title": "登录",
        "login_failure": "登录失败",
        "settings_lang": "语言",
        "open_file": "打开",
        "open_door": "打开",
        "items_one": "{count} 项",
        "items_other": "{count} 项",
        "welcome_emoji": "欢迎 👋",
        "combining": "cafe\u0301",
    },
    "ja": {
        "greeting": "こんにちは、{name}さん！",
        "login_title": "サインイン",
        "login_failure": "サインインに失敗しました",
        "settings_lang": "言語",
        "open_file": "開く",
        "open_door": "開ける",
        "items_one": "{count} 件",
        "items_other": "{count} 件",
        "welcome_emoji": "ようこそ 👋",
        "combining": "cafe\u0301",
    },
    "ko": {
        "greeting": "안녕하세요, {name}님!",
        "login_title": "로그인",
        "login_failure": "로그인 실패",
        "settings_lang": "언어",
        "open_file": "열기",
        "open_door": "열기",
        "items_one": "{count}개 항목",
        "items_other": "{count}개 항목",
        "welcome_emoji": "환영합니다 👋",
        "combining": "cafe\u0301",
    },
    "de": {
        "greeting": "Hallo, {name}!",
        "login_title": "Anmelden",
        "login_failure": "Anmeldung fehlgeschlagen",
        "settings_lang": "Sprache",
        "open_file": "Öffnen",
        "open_door": "Öffnen",
        "items_one": "{count} Element",
        "items_other": "{count} Elemente",
        "welcome_emoji": "Willkommen 👋",
        "combining": "cafe\u0301",
    },
    "fr": {
        "greeting": "Bonjour, {name} !",
        "login_title": "Connexion",
        "login_failure": "Échec de la connexion",
        "settings_lang": "Langue",
        "open_file": "Ouvrir",
        "open_door": "Ouvrir",
        "items_one": "{count} élément",
        "items_other": "{count} éléments",
        "welcome_emoji": "Bienvenue 👋",
        "combining": "cafe\u0301",
    },
    "es": {
        "greeting": "¡Hola, {name}!",
        "login_title": "Iniciar sesión",
        "login_failure": "Error al iniciar sesión",
        "settings_lang": "Idioma",
        "open_file": "Abrir",
        "open_door": "Abrir",
        "items_one": "{count} elemento",
        "items_other": "{count} elementos",
        "welcome_emoji": "Bienvenido 👋",
        "combining": "cafe\u0301",
    },
    "tr": {
        "greeting": "Merhaba, {name}!",
        "login_title": "Oturum aç",
        "login_failure": "Oturum açma başarısız",
        "settings_lang": "Dil",
        "open_file": "Aç",
        "open_door": "Aç",
        "items_one": "{count} öğe",
        "items_other": "{count} öğe",
        "welcome_emoji": "Hoş geldiniz 👋",
        "combining": "cafe\u0301",
    },
}


def messages_for(loc: str) -> list[dict]:
    t = T[loc]
    return [
        {
            "id": "app.greeting",
            "text": t["greeting"],
            "description": "Greeting with user name",
        },
        {"id": "app.login.title", "text": t["login_title"]},
        {"id": "app.login.failure", "text": t["login_failure"]},
        {"id": "app.settings.language", "text": t["settings_lang"]},
        {
            "id": "app.action.open",
            "context": "file",
            "text": t["open_file"],
            "description": "Open a file",
        },
        {
            "id": "app.action.open",
            "context": "door",
            "text": t["open_door"],
            "description": "Open a door",
        },
        {
            "id": "app.items",
            "plural": {"one": t["items_one"], "other": t["items_other"]},
        },
        {"id": "app.welcome_emoji", "text": t["welcome_emoji"]},
        {"id": "app.combining", "text": t["combining"]},
    ]


def main() -> None:
    HERE.mkdir(parents=True, exist_ok=True)
    for loc in sorted(T.keys()):
        doc = {
            "format_version": 1,
            "catalog_id": "robo.demo",
            "catalog_version": "1.0.0",
            "locale": loc,
            "messages": messages_for(loc),
        }
        path = HERE / f"{loc}.robomsg.json"
        path.write_text(
            json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        print("wrote", path.name)


if __name__ == "__main__":
    main()
