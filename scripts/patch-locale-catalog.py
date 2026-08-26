#!/usr/bin/env python3
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "core/src/ui/LocaleCatalog.cpp"

NEW = {
    "zh": {
        "(detached)": "（已分离）",
        "(no machine has paired with this one yet)": "（尚无机器与此机配对）",
        "A machine gets on this list once, and after that it is recognised by its key \u2014 no passcode is asked for again.": "机器只需配对一次，之后凭密钥识别，不再要求通行码。",
        "Allow": "允许",
        "Attached to the shell on this machine.": "已附加到此机上的 Shell。",
        "Bitrate (Mbps)": "码率 (Mbps)",
        "Changing the passcode does NOT turn these machines away \u2014 they no longer use it. Forgetting them is what does.": "更改通行码不会拒绝这些机器——它们已不再使用通行码。只有「忘记」才会移除。",
        "Could not reach the shell host.": "无法连接到 Shell 主机。",
        "Deny": "拒绝",
        "Every machine will have to pair again before it can connect. Continue?": "所有机器都需要重新配对才能连接。是否继续？",
        "FPS": "帧率",
        "Forget every machine": "忘记所有机器",
        "Key": "密钥",
        "Last seen": "上次见到",
        "Let new machines pair with this one": "允许新机器与此机配对",
        "Let this machine in?": "允许此机器接入？",
        "Machine": "机器",
        "Machines allowed to connect to this one": "允许连接此机的机器",
        "Native": "原生分辨率",
        "Open Settings": "打开设置",
        "Opening shell\u2026": "正在打开 Shell\u2026",
        "Paired": "已配对",
        "Quality": "画质",
        "Read this out over the phone to whoever is connecting. It is the one thing a machine in the middle cannot fake.": "通过电话读给正在连接的人。这是中间人无法伪造的信息。",
        "Reattaching shell\u2026": "正在重新附加 Shell\u2026",
        "Share a shell with connected viewers": "与已连接的观看端共享 Shell",
        "Shell closed": "Shell 已关闭",
        "Shell connected": "Shell 已连接",
        "Shell reattached": "Shell 已重新附加",
        "Shell refused.": "Shell 被拒绝。",
        "Stop & attach": "停止并附加",
        "Terminal": "终端",
        "Terminal \u2014 this machine": "终端 \u2014 本机",
        "That shell session is gone.": "该 Shell 会话已不存在。",
        "The host is not sharing a shell.": "主机未共享 Shell。",
        "This machine's key": "本机密钥",
        "Too many shells are open on the host.": "主机上打开的 Shell 过多。",
        "Turn this off once your own machines are paired: a passcode that leaks is then worth nothing, and the machines already on the list keep working.": "自己的机器都配对好后可关闭此项：即使通行码泄露也无妨，列表中的机器仍可连接。",
        "Wrong passcode for shell.": "Shell 通行码错误。",
        "\u2014": "\u2014",
        "attached on this machine": "已在此机上附加",
    },
    "fr": {
        "(detached)": "(détaché)",
        "(no machine has paired with this one yet)": "(aucune machine ne s’est encore appairée)",
        "A machine gets on this list once, and after that it is recognised by its key \u2014 no passcode is asked for again.": "Une machine n’apparaît qu’une fois ; ensuite elle est reconnue par sa clé — plus de code demandé.",
        "Allow": "Autoriser",
        "Attached to the shell on this machine.": "Shell attaché sur cette machine.",
        "Bitrate (Mbps)": "Débit (Mbit/s)",
        "Changing the passcode does NOT turn these machines away \u2014 they no longer use it. Forgetting them is what does.": "Changer le code ne refuse pas ces machines — elles ne l’utilisent plus. Il faut les oublier.",
        "Connected \u2014 choose what to open.": "Connecté — choisissez quoi ouvrir.",
        "Could not reach the shell host.": "Impossible d’atteindre l’hôte shell.",
        "Deny": "Refuser",
        "Every machine will have to pair again before it can connect. Continue?": "Chaque machine devra s’appairer à nouveau. Continuer ?",
        "FPS": "IPS",
        "Forget every machine": "Oublier toutes les machines",
        "Key": "Clé",
        "Last seen": "Vu pour la dernière fois",
        "Let new machines pair with this one": "Autoriser de nouvelles machines à s’appairer",
        "Let this machine in?": "Autoriser cette machine ?",
        "Machine": "Machine",
        "Machines allowed to connect to this one": "Machines autorisées à se connecter",
        "Native": "Natif",
        "Open Settings": "Ouvrir Réglages",
        "Open desktop": "Ouvrir le bureau",
        "Open shell": "Ouvrir le shell",
        "Opening shell\u2026": "Ouverture du shell\u2026",
        "Paired": "Appairé",
        "Quality": "Qualité",
        "Read this out over the phone to whoever is connecting. It is the one thing a machine in the middle cannot fake.": "Lisez ceci au téléphone à la personne qui se connecte. Un intermédiaire ne peut pas le falsifier.",
        "Reattaching shell\u2026": "Rattachement du shell\u2026",
        "Send files": "Envoyer des fichiers",
        "Share a shell with connected viewers": "Partager un shell avec les spectateurs connectés",
        "Shell closed": "Shell fermé",
        "Shell connected": "Shell connecté",
        "Shell reattached": "Shell rattaché",
        "Shell refused.": "Shell refusé.",
        "Stop & attach": "Arrêter et attacher",
        "Terminal": "Terminal",
        "Terminal \u2014 this machine": "Terminal — cette machine",
        "That shell session is gone.": "Cette session shell n’existe plus.",
        "The host is not sharing a shell.": "L’hôte ne partage pas de shell.",
        "This machine's key": "Clé de cette machine",
        "Too many shells are open on the host.": "Trop de shells ouverts sur l’hôte.",
        "Turn this off once your own machines are paired: a passcode that leaks is then worth nothing, and the machines already on the list keep working.": "Désactivez une fois vos machines appairées : un code qui fuite ne sert plus à rien, celles déjà listées restent connectées.",
        "Wrong passcode for shell.": "Mauvais code pour le shell.",
        "\u2014": "\u2014",
        "attached on this machine": "attaché sur cette machine",
    },
    "de": {
        "(detached)": "(getrennt)",
        "(no machine has paired with this one yet)": "(noch keine Maschine gekoppelt)",
        "A machine gets on this list once, and after that it is recognised by its key \u2014 no passcode is asked for again.": "Eine Maschine steht einmal auf der Liste und wird danach per Schlüssel erkannt — kein Code mehr nötig.",
        "Allow": "Erlauben",
        "Attached to the shell on this machine.": "Shell an diese Maschine angehängt.",
        "Bitrate (Mbps)": "Bitrate (Mbit/s)",
        "Changing the passcode does NOT turn these machines away \u2014 they no longer use it. Forgetting them is what does.": "Ein neuer Code wirft diese Maschinen nicht raus — sie nutzen ihn nicht mehr. Erst Vergessen entfernt sie.",
        "Connected \u2014 choose what to open.": "Verbunden — wählen Sie, was geöffnet werden soll.",
        "Could not reach the shell host.": "Shell-Host nicht erreichbar.",
        "Deny": "Ablehnen",
        "Every machine will have to pair again before it can connect. Continue?": "Jede Maschine muss sich neu koppeln. Fortfahren?",
        "FPS": "FPS",
        "Forget every machine": "Alle Maschinen vergessen",
        "Key": "Schlüssel",
        "Last seen": "Zuletzt gesehen",
        "Let new machines pair with this one": "Neue Maschinen koppeln lassen",
        "Let this machine in?": "Diese Maschine zulassen?",
        "Machine": "Maschine",
        "Machines allowed to connect to this one": "Maschinen mit Verbindungsrecht",
        "Native": "Nativ",
        "Open Settings": "Einstellungen öffnen",
        "Open desktop": "Desktop öffnen",
        "Open shell": "Shell öffnen",
        "Opening shell\u2026": "Shell wird geöffnet\u2026",
        "Paired": "Gekoppelt",
        "Quality": "Qualität",
        "Read this out over the phone to whoever is connecting. It is the one thing a machine in the middle cannot fake.": "Lesen Sie dies am Telefon vor. Ein Mittelsmann kann es nicht fälschen.",
        "Reattaching shell\u2026": "Shell wird wieder angehängt\u2026",
        "Send files": "Dateien senden",
        "Share a shell with connected viewers": "Shell mit verbundenen Betrachtern teilen",
        "Shell closed": "Shell geschlossen",
        "Shell connected": "Shell verbunden",
        "Shell reattached": "Shell wieder angehängt",
        "Shell refused.": "Shell abgelehnt.",
        "Stop & attach": "Stoppen und anhängen",
        "Terminal": "Terminal",
        "Terminal \u2014 this machine": "Terminal — diese Maschine",
        "That shell session is gone.": "Diese Shell-Sitzung existiert nicht mehr.",
        "The host is not sharing a shell.": "Der Host teilt keine Shell.",
        "This machine's key": "Schlüssel dieser Maschine",
        "Too many shells are open on the host.": "Zu viele Shells auf dem Host geöffnet.",
        "Turn this off once your own machines are paired: a passcode that leaks is then worth nothing, and the machines already on the list keep working.": "Nach dem Koppeln Ihrer Maschinen ausschalten: ein geleakter Code nützt nichts, gelistete Maschinen bleiben verbunden.",
        "Wrong passcode for shell.": "Falscher Code für die Shell.",
        "\u2014": "\u2014",
        "attached on this machine": "an diese Maschine angehängt",
    },
    "ru": {
        "(detached)": "(отсоединено)",
        "(no machine has paired with this one yet)": "(ещё ни одна машина не сопряжена)",
        "A machine gets on this list once, and after that it is recognised by its key \u2014 no passcode is asked for again.": "Машина попадает в список один раз, затем узнаётся по ключу — код больше не спрашивается.",
        "Allow": "Разрешить",
        "Attached to the shell on this machine.": "Оболочка подключена на этой машине.",
        "Bitrate (Mbps)": "Битрейт (Мбит/с)",
        "Changing the passcode does NOT turn these machines away \u2014 they no longer use it. Forgetting them is what does.": "Смена кода не отключает эти машины — они его не используют. Удалить можно только через «Забыть».",
        "Connected \u2014 choose what to open.": "Подключено — выберите, что открыть.",
        "Could not reach the shell host.": "Не удалось достичь хоста оболочки.",
        "Deny": "Отклонить",
        "Every machine will have to pair again before it can connect. Continue?": "Всем машинам придётся сопрячься заново. Продолжить?",
        "FPS": "Кадр/с",
        "Forget every machine": "Забыть все машины",
        "Key": "Ключ",
        "Last seen": "Последний раз",
        "Let new machines pair with this one": "Разрешить сопряжение новых машин",
        "Let this machine in?": "Разрешить эту машину?",
        "Machine": "Машина",
        "Machines allowed to connect to this one": "Машины с разрешением подключения",
        "Native": "Исходное",
        "Open Settings": "Открыть настройки",
        "Open desktop": "Открыть рабочий стол",
        "Open shell": "Открыть оболочку",
        "Opening shell\u2026": "Открытие оболочки\u2026",
        "Paired": "Сопряжено",
        "Quality": "Качество",
        "Read this out over the phone to whoever is connecting. It is the one thing a machine in the middle cannot fake.": "Продиктуйте по телефону подключающемуся. Это нельзя подделать посередине.",
        "Reattaching shell\u2026": "Повторное подключение оболочки\u2026",
        "Send files": "Отправить файлы",
        "Share a shell with connected viewers": "Делиться оболочкой с подключёнными зрителями",
        "Shell closed": "Оболочка закрыта",
        "Shell connected": "Оболочка подключена",
        "Shell reattached": "Оболочка снова подключена",
        "Shell refused.": "Оболочка отклонена.",
        "Stop & attach": "Остановить и подключить",
        "Terminal": "Терминал",
        "Terminal \u2014 this machine": "Терминал — эта машина",
        "That shell session is gone.": "Эта сессия оболочки исчезла.",
        "The host is not sharing a shell.": "Хост не делится оболочкой.",
        "This machine's key": "Ключ этой машины",
        "Too many shells are open on the host.": "На хосте открыто слишком много оболочек.",
        "Turn this off once your own machines are paired: a passcode that leaks is then worth nothing, and the machines already on the list keep working.": "Отключите, когда свои машины сопряжены: утечка кода бессмысленна, машины в списке останутся.",
        "Wrong passcode for shell.": "Неверный код для оболочки.",
        "\u2014": "\u2014",
        "attached on this machine": "подключено на этой машине",
    },
    "ja": {
        "(detached)": "（切り離し）",
        "(no machine has paired with this one yet)": "（まだペアリングされたマシンはありません）",
        "A machine gets on this list once, and after that it is recognised by its key \u2014 no passcode is asked for again.": "マシンは一度リストに載ると、以後はキーで識別され、パスコードは不要です。",
        "Allow": "許可",
        "Attached to the shell on this machine.": "このマシンのシェルに接続しました。",
        "Bitrate (Mbps)": "ビットレート (Mbps)",
        "Changing the passcode does NOT turn these machines away \u2014 they no longer use it. Forgetting them is what does.": "パスコードを変えてもこれらのマシンは拒否されません。忘れる操作で削除します。",
        "Connected \u2014 choose what to open.": "接続済み — 開くものを選んでください。",
        "Could not reach the shell host.": "シェルホストに到達できません。",
        "Deny": "拒否",
        "Every machine will have to pair again before it can connect. Continue?": "すべてのマシンが再ペアリング必要です。続行しますか？",
        "FPS": "FPS",
        "Forget every machine": "すべてのマシンを忘れる",
        "Key": "キー",
        "Last seen": "最終確認",
        "Let new machines pair with this one": "新しいマシンのペアリングを許可",
        "Let this machine in?": "このマシンを許可しますか？",
        "Machine": "マシン",
        "Machines allowed to connect to this one": "接続を許可されたマシン",
        "Native": "ネイティブ",
        "Open Settings": "設定を開く",
        "Open desktop": "デスクトップを開く",
        "Open shell": "シェルを開く",
        "Opening shell\u2026": "シェルを開いています…",
        "Paired": "ペア済み",
        "Quality": "画質",
        "Read this out over the phone to whoever is connecting. It is the one thing a machine in the middle cannot fake.": "接続する人に電話で読み上げてください。中間者は偽造できません。",
        "Reattaching shell\u2026": "シェルを再接続しています…",
        "Send files": "ファイルを送信",
        "Share a shell with connected viewers": "接続した視聴者とシェルを共有",
        "Shell closed": "シェルを閉じました",
        "Shell connected": "シェル接続済み",
        "Shell reattached": "シェルを再接続しました",
        "Shell refused.": "シェルが拒否されました。",
        "Stop & attach": "停止して接続",
        "Terminal": "ターミナル",
        "Terminal \u2014 this machine": "ターミナル — このマシン",
        "That shell session is gone.": "そのシェルセッションはありません。",
        "The host is not sharing a shell.": "ホストはシェルを共有していません。",
        "This machine's key": "このマシンのキー",
        "Too many shells are open on the host.": "ホストで開いているシェルが多すぎます。",
        "Turn this off once your own machines are paired: a passcode that leaks is then worth nothing, and the machines already on the list keep working.": "自分のマシンがペア済みならオフに。コードが漏れてもリスト内は接続可能です。",
        "Wrong passcode for shell.": "シェルのパスコードが違います。",
        "\u2014": "\u2014",
        "attached on this machine": "このマシンに接続",
    },
    "ko": {
        "(detached)": "(분리됨)",
        "(no machine has paired with this one yet)": "(아직 페어링된 기기 없음)",
        "A machine gets on this list once, and after that it is recognised by its key \u2014 no passcode is asked for again.": "기기는 한 번 등록되면 키로 인식되며 다시 암호를 묻지 않습니다.",
        "Allow": "허용",
        "Attached to the shell on this machine.": "이 기기의 셸에 연결됨.",
        "Bitrate (Mbps)": "비트레이트 (Mbps)",
        "Changing the passcode does NOT turn these machines away \u2014 they no longer use it. Forgetting them is what does.": "암호를 바꿔도 목록의 기기는 차단되지 않습니다. 잊기로 제거합니다.",
        "Connected \u2014 choose what to open.": "연결됨 — 열 항목을 선택하세요.",
        "Could not reach the shell host.": "셸 호스트에 연결할 수 없습니다.",
        "Deny": "거부",
        "Every machine will have to pair again before it can connect. Continue?": "모든 기기가 다시 페어링해야 합니다. 계속할까요?",
        "FPS": "FPS",
        "Forget every machine": "모든 기기 잊기",
        "Key": "키",
        "Last seen": "마지막 확인",
        "Let new machines pair with this one": "새 기기 페어링 허용",
        "Let this machine in?": "이 기기를 허용할까요?",
        "Machine": "기기",
        "Machines allowed to connect to this one": "연결 허용된 기기",
        "Native": "네이티브",
        "Open Settings": "설정 열기",
        "Open desktop": "데스크톱 열기",
        "Open shell": "셸 열기",
        "Opening shell\u2026": "셸 여는 중…",
        "Paired": "페어링됨",
        "Quality": "화질",
        "Read this out over the phone to whoever is connecting. It is the one thing a machine in the middle cannot fake.": "연결하는 사람에게 전화로 읽어 주세요. 중간자는 위조할 수 없습니다.",
        "Reattaching shell\u2026": "셸 다시 연결 중…",
        "Send files": "파일 보내기",
        "Share a shell with connected viewers": "연결된 시청자와 셸 공유",
        "Shell closed": "셸 닫힘",
        "Shell connected": "셸 연결됨",
        "Shell reattached": "셸 다시 연결됨",
        "Shell refused.": "셸이 거부됨.",
        "Stop & attach": "중지 후 연결",
        "Terminal": "터미널",
        "Terminal \u2014 this machine": "터미널 — 이 기기",
        "That shell session is gone.": "해당 셸 세션이 없습니다.",
        "The host is not sharing a shell.": "호스트가 셸을 공유하지 않습니다.",
        "This machine's key": "이 기기의 키",
        "Too many shells are open on the host.": "호스트에 열린 셸이 너무 많습니다.",
        "Turn this off once your own machines are paired: a passcode that leaks is then worth nothing, and the machines already on the list keep working.": "내 기기 페어링 후 끄세요. 암호가 유출돼도 목록의 기기는 연결됩니다.",
        "Wrong passcode for shell.": "셸 암호가 틀렸습니다.",
        "\u2014": "\u2014",
        "attached on this machine": "이 기기에 연결됨",
    },
    "ar": {
        "(detached)": "(منفصل)",
        "(no machine has paired with this one yet)": "(لم يُقترن أي جهاز بعد)",
        "A machine gets on this list once, and after that it is recognised by its key \u2014 no passcode is asked for again.": "يُضاف الجهاز مرة واحدة ثم يُعرَف بمفتاحه — لا يُطلب رمز المرور مجدداً.",
        "Allow": "سماح",
        "Attached to the shell on this machine.": "تم ربط الصدفة على هذا الجهاز.",
        "Bitrate (Mbps)": "معدل البت (ميجابت/ث)",
        "Changing the passcode does NOT turn these machines away \u2014 they no longer use it. Forgetting them is what does.": "تغيير الرمز لا يطرد هذه الأجهزة — لم تعد تستخدمه. النسيان يزيلها.",
        "Connected \u2014 choose what to open.": "متصل — اختر ما تريد فتحه.",
        "Could not reach the shell host.": "تعذّر الوصول إلى مضيف الصدفة.",
        "Deny": "رفض",
        "Every machine will have to pair again before it can connect. Continue?": "يجب على كل جهاز الاقتران مجدداً. هل تتابع؟",
        "FPS": "إطار/ث",
        "Forget every machine": "نسيان كل الأجهزة",
        "Key": "المفتاح",
        "Last seen": "آخر ظهور",
        "Let new machines pair with this one": "السماح باقتران أجهزة جديدة",
        "Let this machine in?": "هل تسمح لهذا الجهاز؟",
        "Machine": "جهاز",
        "Machines allowed to connect to this one": "الأجهزة المسموح لها بالاتصال",
        "Native": "أصلي",
        "Open Settings": "فتح الإعدادات",
        "Open desktop": "فتح سطح المكتب",
        "Open shell": "فتح الصدفة",
        "Opening shell\u2026": "جارٍ فتح الصدفة…",
        "Paired": "مقترن",
        "Quality": "الجودة",
        "Read this out over the phone to whoever is connecting. It is the one thing a machine in the middle cannot fake.": "اقرأ هذا هاتفياً لمن يتصل. لا يمكن تزييفه في الوسط.",
        "Reattaching shell\u2026": "جارٍ إعادة ربط الصدفة…",
        "Send files": "إرسال ملفات",
        "Share a shell with connected viewers": "مشاركة صدفة مع المشاهدين المتصلين",
        "Shell closed": "أُغلقت الصدفة",
        "Shell connected": "اتصلت الصدفة",
        "Shell reattached": "أُعيد ربط الصدفة",
        "Shell refused.": "رُفضت الصدفة.",
        "Stop & attach": "إيقاف وربط",
        "Terminal": "طرفية",
        "Terminal \u2014 this machine": "طرفية — هذا الجهاز",
        "That shell session is gone.": "جلسة الصدفة هذه لم تعد موجودة.",
        "The host is not sharing a shell.": "المضيف لا يشارك صدفة.",
        "This machine's key": "مفتاح هذا الجهاز",
        "Too many shells are open on the host.": "صدفات كثيرة مفتوحة على المضيف.",
        "Turn this off once your own machines are paired: a passcode that leaks is then worth nothing, and the machines already on the list keep working.": "أوقفه بعد اقتران أجهزتك: تسريب الرمز لا يفيد، والأجهزة في القائمة تبقى متصلة.",
        "Wrong passcode for shell.": "رمز مرور خاطئ للصدفة.",
        "\u2014": "\u2014",
        "attached on this machine": "مرتبط على هذا الجهاز",
    },
}

LANG_MAP = {
    "k_zh": "zh",
    "k_fr": "fr",
    "k_de": "de",
    "k_ru": "ru",
    "k_ja": "ja",
    "k_ko": "ko",
    "k_ar": "ar",
}


def decode_cpp(raw: str) -> str:
    out = bytearray()
    i = 0
    while i < len(raw):
        if raw.startswith("\\x", i) and i + 3 < len(raw):
            out.append(int(raw[i + 2 : i + 4], 16))
            i += 4
            continue
        ch = raw[i]
        if ch == "\\" and i + 1 < len(raw):
            nxt = raw[i + 1]
            if nxt in ('"', "\\"):
                out.append(ord(nxt))
                i += 2
                continue
        out.extend(ch.encode("utf-8"))
        i += 1
    return out.decode("utf-8")


def parse_block(text: str) -> dict[str, str]:
    entries: dict[str, str] = {}
    for en_raw, tr_raw in re.findall(r'\{"((?:\\.|[^"\\])*)", "((?:\\.|[^"\\])*)"\}', text):
        entries[decode_cpp(en_raw)] = decode_cpp(tr_raw)
    return entries


def encode_cpp(text: str, ascii_only: bool = False) -> str:
    parts: list[str] = []
    for ch in text:
        o = ord(ch)
        if ch == "\\":
            parts.append("\\\\")
        elif ch == '"':
            parts.append('\\"')
        elif o < 0x80 or not ascii_only:
            parts.append(ch)
        else:
            for byte in ch.encode("utf-8"):
                parts.append(f"\\x{byte:02x}")
    return "".join(parts)


def format_block(entries: dict[str, str]) -> str:
    lines = []
    for en in sorted(entries):
        lines.append(f'    {{"{encode_cpp(en, ascii_only=True)}", "{encode_cpp(entries[en])}"}},')
    return "\n".join(lines)


def main() -> None:
    text = CATALOG.read_text(encoding="utf-8")
    for array, lang in LANG_MAP.items():
        pattern = rf"static const CatalogEntry {array}\[\] = \{{\n(.*?)\n\}};"
        match = re.search(pattern, text, re.S)
        if not match:
            raise SystemExit(f"missing block {array}")
        entries = parse_block(match.group(1))
        entries.update(NEW[lang])
        replacement = f"static const CatalogEntry {array}[] = {{\n{format_block(entries)}\n}};"
        text = text[: match.start()] + replacement + text[match.end() :]
    CATALOG.write_text(text, encoding="utf-8", newline="\n")
    print("patched", CATALOG)


if __name__ == "__main__":
    main()
