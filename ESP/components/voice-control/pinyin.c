/*
 * 汉字 -> 拼音转换
 * 基于 GB2312 一级字库静态表（带声调，按 Unicode 码点排序），
 * 用于语音命令分组：同音/同字的分机归为一组。
 * 名称含非中文字符时降级为原样拷贝。
 */

#include "pinyin.h"
#include <string.h>

/* 生成的拼音表（见 pinyin_table.inc） */
#include "pinyin_table.inc"

/* 二分查找：codepoint -> 拼音，找不到返回 NULL */
static const char *pinyin_lookup(uint32_t cp)
{
    unsigned int lo = 0, hi = pinyin_table_size;
    while (lo < hi) {
        unsigned int mid = (lo + hi) / 2;
        if (pinyin_table[mid].codepoint < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < pinyin_table_size && pinyin_table[lo].codepoint == cp)
        return pinyin_table[lo].pinyin;
    return NULL;
}

/* 阿拉伯数字 -> 拼音（带声调） */
static const char *digit_pinyin(char c)
{
    switch (c) {
        case '0': return "líng";
        case '1': return "yī";
        case '2': return "èr";
        case '3': return "sān";
        case '4': return "sì";
        case '5': return "wǔ";
        case '6': return "liù";
        case '7': return "qī";
        case '8': return "bā";
        case '9': return "jiǔ";
        default:  return NULL;
    }
}

/* UTF-8 首字节 -> 字符长度，非法返回 -1 */
static int utf8_char_len(unsigned char c)
{
    if (c < 0x80) return 1;
    else if ((c & 0xE0) == 0xC0) return 2;
    else if ((c & 0xF0) == 0xE0) return 3;
    else if ((c & 0xF8) == 0xF0) return 4;
    return -1;
}

/* 解析一个 UTF-8 字符为 codepoint，返回字节长度，失败返回 -1 */
static int utf8_decode(const char *s, uint32_t *cp)
{
    unsigned char c = (unsigned char)s[0];
    int len = utf8_char_len(c);
    if (len < 0) return -1;

    uint32_t v;
    if (len == 1) {
        *cp = c;
        return 1;
    }
    if (len == 2) v = c & 0x1F;
    else if (len == 3) v = c & 0x0F;
    else v = c & 0x07;

    for (int k = 1; k < len; k++) {
        unsigned char cc = (unsigned char)s[k];
        if ((cc & 0xC0) != 0x80) return -1;
        v = (v << 6) | (cc & 0x3F);
    }
    *cp = v;
    return len;
}

/*
 * 将 UTF-8 名称转换为拼音 key。
 * - 中文字符：转拼音
 * - 阿拉伯数字：转拼音（如 "2" -> "èr"）
 * - 含英文/标点等其他字符：原样拷贝返回（降级）
 */
size_t pinyin_of_name(const char *utf8_name, char *out, size_t out_len)
{
    size_t in_len = strlen(utf8_name);
    size_t i = 0, o = 0;
    int convertible = 1;

    if (!out || out_len == 0) return 0;
    out[0] = 0;

    /* 第一遍：检查是否全为可转拼音的字符（中文或阿拉伯数字） */
    while (i < in_len) {
        unsigned char c = (unsigned char)utf8_name[i];
        if (c >= '0' && c <= '9') { i++; continue; }       /* 数字 */
        uint32_t cp;
        int len = utf8_decode(utf8_name + i, &cp);
        if (len < 0 || cp < 0x4E00 || cp > 0x9FFF || pinyin_lookup(cp) == NULL) {
            convertible = 0;
            break;
        }
        i += len;
    }

    if (!convertible) {
        strncpy(out, utf8_name, out_len - 1);
        out[out_len - 1] = 0;
        return strlen(out);
    }

    /* 第二遍：逐字符转拼音，空格分隔 */
    i = 0;
    while (i < in_len) {
        unsigned char c = (unsigned char)utf8_name[i];
        const char *py;
        size_t clen;

        if (c >= '0' && c <= '9') {
            py = digit_pinyin((char)c);
            clen = 1;
        } else {
            uint32_t cp;
            clen = (size_t)utf8_decode(utf8_name + i, &cp);
            py = pinyin_lookup(cp);
        }
        if (!py) { i += clen; continue; }

        size_t need = strlen(py);
        if (o > 0 && o < out_len - 1) out[o++] = ' ';          /* 字符间空格 */
        if (o + need >= out_len) break;                        /* 防溢出 */
        memcpy(out + o, py, need);
        o += need;
        i += clen;
    }
    out[o] = 0;
    return o;
}
