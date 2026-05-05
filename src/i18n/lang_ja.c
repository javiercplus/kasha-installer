/*
 * lang_ja.c
 * Japanese translations
 */
#include "lang.h"
#include <string.h>
#include <stddef.h>

static const LangInfo lang_ja = {
    .code = "ja",
    .name = "日本語",

    // Welcome
    .welcome_title = "Neko-Voidへようこそ",
    .welcome_body = "非公式のVoid Linuxリスピン、軽量で高速、ゲーム体験が可能。",
    .welcome_title_markup = "<span size='xx-large' weight='bold'>Neko-Voidへようこそ</span>",
    .welcome_body_markup = "<span size='large'>非公式のVoid Linuxリスピン、軽量で高速、ゲーム体験が可能。</span>",

    // Install Type
    .install_type_title = "インストールタイプを選択:",
    .clean_install = "クリーンインストール (ディスクを消去)",
    .install_alongside = "他のOSと一緒にインストール (リサイズ)",
    .clean_install_desc = "選択したディスクのすべてのデータが消去され、パーティションが自動的に作成されます。",
    .alongside_desc = "既存のパーティションをリサイズして新しいシステムの空き領域を作成します。",

    // Partitions
    .disk = "ディスク:",
    .disk_note = "注意: GPartedでパーティションを変更した後、下で設定できます。",
    .existing_parts = "既存のパーティション",
    .install_parts = "インストールパーティション",
    .manual_partitioning = "パーティションを手動で設定",
    .btn_add = "追加",
    .btn_edit = "編集",
    .btn_delete = "削除",
    .btn_reset = "リセット",
    .btn_gparted = "パーティショナー (GPointed)",
    .part_mount = "マウントポイント:",

    // Bootloader
    .boot_detect = "ファームウェアを検出中...",
    .grub_install = "GRUBをインストール:",

    // System
    .hostname = "ホスト名:",
    .country = "国:",
    .locale = "ロケール:",
    .region = "地域:",
    .city = "都市:",
    .timezone = "タイムゾーン:",

    // Users
    .root_pass = "rootパスワード:",
    .user_account = "ユーザーアカウント:",
    .fullname = "フルネーム:",
    .username = "ユーザー名:",
    .password = "パスワード:",
    .confirm = "確認:",
    .autologin = "自動ログインを有効にする",

    // Install
    .install_btn = "インストール開始",
    .reboot_btn = "システム再起動",
    .back = "戻る",
    .next = "次へ",

    // Tabs
    .tab_welcome = "ようこそ",
    .tab_install_type = "種別",
    .tab_partitions = "パーティション",
    .tab_bootloader = "ブート",
    .tab_system = "システム",
    .tab_users = "ユーザー",
    .tab_install = "インストール",

    // Partition dialog
    .select_partition = "パーティションを選択:",
    .filesystem = "ファイルシステム:",
    .mount_point = "マウントポイント:",
    .format_partition = "パーティションをフォーマット?",
    .encrypt_luks = "暗号化 (LUKS)?",
    .general = "一般",
    .encryption = "暗号化",
    .dialog_add = "パーティションを追加",
    .dialog_edit = "パーティションを編集",
    .dialog_update = "更新",
    .save = "_追加",
    .cancel = "_キャンセル",

    // Messages
    .error_root_password = "エラー: rootパスワードがありません。",
    .error_no_partitions = "エラー: パーティションが設定されていません。",
    .error_no_root = "エラー: ルート (/) パーティションが設定されていません。",
    .error_usr_not_supported = "エラー: /usrは別パーティションとしてサポートされていません!",
    .error_efi_partition = "エラー: EFIにはEFIシステムパーティション (/boot/efi) が必要です!",

    // Window title
    .window_title = "Kasha インストーラー - Neko Void",

    // Success
    .success_title = "NEKO-VOID 準備完了!!!",
    .success_body = "インストールが完了しました。\nシステムは再起動する準備ができました。",
    .success_reboot = "今すぐ再起動",
};

const LangInfo* lang_ja_module(void) {
    return &lang_ja;
}
