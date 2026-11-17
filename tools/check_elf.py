#!/usr/bin/env python3
"""Static ELF inspection: prove the Kindle binary has no dynamic loader,
no DT_NEEDED entries and no undefined symbols."""
import os
import struct
import sys

DEFAULT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "build", "hipda")
PATH = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
SHT_SYMTAB, SHT_DYNSYM, SHT_STRTAB = 2, 11, 3
PT_INTERP, PT_DYNAMIC, PT_LOAD = 3, 2, 1


def main() -> int:
    if not os.path.exists(PATH):
        print("missing", PATH, file=sys.stderr)
        return 2
    b = open(PATH, "rb").read()
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
     e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
     e_shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", b, 16)

    print("file       :", PATH)
    print("size       :", len(b), "bytes")
    print("class      :", "ELF32" if b[4] == 1 else "ELF64")
    print("endian     :", "little" if b[5] == 1 else "big")
    print("type/machine: %d / 0x%02X (%s)" % (e_type, e_machine,
                                              "ARM" if e_machine == 40 else "?"))
    print("entry      : 0x%08X" % e_entry)

    loads, interp, dynamic = 0, [], []
    for i in range(e_phnum):
        p = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
            struct.unpack_from("<IIIIIIII", b, p)
        if p_type == PT_LOAD:
            loads += 1
            print("  PT_LOAD  off=0x%06X vaddr=0x%08X filesz=%-7d flags=%s" %
                  (p_offset, p_vaddr, p_filesz,
                   "".join(c for c, f in (("R", 4), ("W", 2), ("X", 1)) if p_flags & f)))
        elif p_type == PT_INTERP:
            interp.append(b[p_offset:p_offset + p_filesz].rstrip(b"\0").decode())
        elif p_type == PT_DYNAMIC:
            dynamic.append(p_offset)

    print("PT_INTERP  :", interp or "none (fully static)")
    print("PT_DYNAMIC :", dynamic or "none")

    # section headers + undefined symbols
    sections = []
    for i in range(e_shnum):
        s = e_shoff + i * e_shentsize
        (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
         sh_link, sh_info, sh_addralign, sh_entsize) = struct.unpack_from("<IIIIIIIIII", b, s)
        sections.append((sh_type, sh_offset, sh_size, sh_link, sh_entsize))

    # .ARM.attributes -> Tag_CPU_arch (6). 8 = ARMv6, 10 = ARMv7.
    cpu_arch = None
    for sh_type, off, size, _link, _entsize in sections:
        if sh_type != 0x70000003:  # SHT_ARM_ATTRIBUTES
            continue
        d = b[off:off + size]
        for i in range(len(d) - 1):
            if d[i] == 6 and d[i + 1] in (7, 8, 9, 10, 11, 12):
                cpu_arch = d[i + 1]
                break
        if cpu_arch:
            break
    arch_name = {7: "ARMv5", 8: "ARMv6", 9: "ARMv6KZ", 10: "ARMv7",
                 11: "ARMv6-M", 12: "ARMv7S"}.get(cpu_arch, "?")
    print("ARM arch   : Tag_CPU_arch=%s (%s)" % (cpu_arch, arch_name))

    key_symbols = [
        "main", "app_run", "app_selftest", "net_fetch", "net_login",
        "discuz_parse_thread", "discuz_parse_topic_list",
        "book_from_thread", "book_finalize", "font_draw_text",
        "render_page", "ui_present", "input_read",
        "fbink_open", "fbink_init", "fbink_get_state",
        "fbink_print_raw_data", "fbink_close",
    ]
    defined = set()
    undef = []
    symtab_found = any(s[0] == SHT_SYMTAB for s in sections)
    for sh_type, off, size, link, entsize in sections:
        if sh_type not in (SHT_SYMTAB, SHT_DYNSYM):
            continue
        str_type, str_off, str_size, _, _ = sections[link]
        strings = b[str_off:str_off + str_size]
        step = entsize or 16
        for pos in range(off, off + size, step):
            st_name, st_value, st_size, st_info, st_other, st_shndx = \
                struct.unpack_from("<IIIBBH", b, pos)
            if not st_name:
                continue
            name = strings[st_name:strings.find(b"\0", st_name)].decode("ascii", "replace")
            if not name:
                continue
            if st_shndx == 0:
                undef.append(name)
            else:
                defined.add(name)
    print("undefined symbols:", len(undef), sorted(set(undef))[:12])
    # zig strips the static binary, so a missing .symtab is expected. In that
    # case verify linkage by looking for the payload strings in .rodata.
    missing = []
    if symtab_found:
        missing = [s for s in key_symbols if s not in defined]
        print("key symbols : %d/%d present%s" % (
            len(key_symbols) - len(missing), len(key_symbols),
            " (missing: %s)" % ", ".join(missing) if missing else ""))
    else:
        print("key symbols : n/a (stripped; link success proves resolution)")
    fingerprints = [b"/dev/fb0", b"mxcfb", b"HUF1",
                    b"forumdisplay.php", b"HiPDA"]
    found = [t.decode() for t in fingerprints if t in b]
    print("payload     : %d/%d markers (%s)" % (len(found), len(fingerprints),
                                                ", ".join(found)))

    ok = (b[:4] == b"\x7fELF" and b[4] == 1 and b[5] == 1 and
          e_type == 2 and e_machine == 40 and not interp and not dynamic and
          loads >= 2 and not undef and not missing and
          len(found) == len(fingerprints) and cpu_arch in (7, 8, 9, 10))
    print("verdict    :", "STATIC ARM EXECUTABLE - OK" if ok else "CHECK FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
