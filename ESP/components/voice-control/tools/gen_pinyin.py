#!/usr/bin/env python3
"""
Generate pinyin table for GB2312 level-1 Chinese characters (3755 chars).
Output: a C file with a sorted array keyed by Unicode codepoint.
Run:  python3 tools/gen_pinyin.py > pinyin_table.inc
"""
import sys
from pypinyin import pinyin, Style

OUT_HEADER = r"""/*
 * 自动生成的汉字->拼音表 (GB2312 一级字库, 带声调)
 * 由 tools/gen_pinyin.py 生成, 请勿手动编辑
 */
#ifndef PINYIN_TABLE_H
#define PINYIN_TABLE_H

#include <stdint.h>

typedef struct {
    uint32_t codepoint;   /* Unicode 码点 (UTF-8 解码后的值) */
    const char *pinyin;   /* 带声调的拼音 */
} pinyin_entry_t;

extern const pinyin_entry_t pinyin_table[];
extern const unsigned int pinyin_table_size;

#endif
"""


def main():
    entries = []
    for hi in range(0xB0, 0xD7 + 1):
        for lo in range(0xA1, 0xFE + 1):
            try:
                ch = bytes([hi, lo]).decode('gb2312')
            except UnicodeDecodeError:
                continue
            py = pinyin(ch, style=Style.TONE, heteronym=False)
            if not py or not py[0]:
                continue
            cp = ord(ch)
            entries.append((cp, py[0][0]))

    # 去重（多音字表只留一个读音）并按 codepoint 排序
    seen = {}
    for cp, py in entries:
        if cp not in seen:
            seen[cp] = py
    entries = sorted(seen.items())

    print(OUT_HEADER)
    for cp, py in entries:
        print(f'    {{ 0x{cp:04X}, "{py}" }},')
    print(f'}};\nconst unsigned int pinyin_table_size = {len(entries)};')

    sys.stderr.write(f'generated {len(entries)} pinyin entries\n')


if __name__ == '__main__':
    main()
