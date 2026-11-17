#!/usr/bin/env python3
"""Build synthetic Discuz! 7.2 pages for parser regression tests + preview.

IMPORTANT: these are SYNTHETIC fixtures, not pages scraped from HiPDA. They
follow the Discuz! 7.2 default template and the Jsoup selectors used by
GreenSkinMonster/hipda (HiParserThreadList / HiParserThreadDetail). They let us
test the C parser offline, because the real forum requires a login (verified
2026-11-15) and index.php is behind a Cloudflare JS challenge.

The real captured responses are kept alongside them as *_real.html.
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = os.path.join(ROOT, "fixtures")
os.makedirs(FIX, exist_ok=True)

HEAD = """<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN"
 "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=gbk" />
<meta name="generator" content="Discuz! 7.2" />
<title>{title} - 4D4Y</title>
</head><body>
<div id="wrap"><div id="nav">4D4Y &raquo; Discovery</div>
"""
TAIL = """</div></body></html>
"""

PAGER = """<div class="forumcontrol"><div class="pages">
<a href="forumdisplay.php?fid=2&page=1">1</a>
<a href="forumdisplay.php?fid=2&page=2">2</a>
<a href="forumdisplay.php?fid=2&page=3">3</a>
<a href="forumdisplay.php?fid=2&page=4">4</a>
<a href="forumdisplay.php?fid=2&page=5">5</a>
<strong>{cur}</strong>
<a class="next" href="forumdisplay.php?fid=2&page={next}">&rsaquo;&rsaquo;</a>
</div></div>
"""

THREAD_PAGER = """<div class="forumcontrol"><div class="pages">
<a href="viewthread.php?tid=1579403&page=1">1</a>
<a href="viewthread.php?tid=1579403&page=2">2</a>
<a href="viewthread.php?tid=1579403&page=3">3</a>
<strong>{cur}</strong>
</div></div>
"""

TITLES = [
    "Kindle 越狱后还能干什么？聊聊这几年的折腾",
    "KPW5 换电池实录，附拆解图",
    "求推荐适合墨水屏的中文正文字体",
    "【闲聊】十年了，HiPDA 还是那个味儿",
    "E-INK 屏幕刷新波形到底有多少种",
    "把旧 Kindle 做成常驻信息屏的踩坑记录",
    "关于 Discuz! 7.2 的 archiver 静态页",
    "二手区交易须知（请先读这个）",
    "自制 6 寸墨水屏保护壳，3D 打印文件分享",
    "周末读书笔记：关于纸张质感的执念",
    "无人问津的小众设备收藏分享",
    "请教：USBNetwork 之后 SSH 连不上怎么办",
    "固件 5.18 越狱方式汇总与讨论",
    "一张图看懂电子纸的灰度与抖动",
    "聊聊论坛客户端的整页翻页交互",
    "旧书扫描 OCR 的工作流记录",
    "给 Kindle 写原生程序的那些坑",
    "关于中文断行与标点避头尾",
    "低温环境下墨水屏残影观察",
    "深夜水帖：你们的阅读器都吃什么灰",
    "分享一个 GBK 转 UTF-8 的小工具",
    "关于电池循环寿命的长期数据",
]
AUTHORS = ["墨水瓶", "老陈", "wind", "阿枢", "pda_fan", "书虫", "K", "小白"]

TOPIC_ROW = """<tbody id="{kind}_{tid}">
<tr>
<td class="folder"><img src="images/folder_new.gif" /></td>
<td class="icon">&nbsp;</td>
<th class="subject">
  <span><a href="viewthread.php?tid={tid}">{title}</a></span>
  {attach}
  <span class="threadpages"><a href="viewthread.php?tid={tid}&page=2">2</a></span>
</th>
<td class="author"><cite><a href="space.php?uid={uid}">{author}</a></cite><em>2026-11-14</em></td>
<td class="nums"><strong>{replies}</strong><em>{views}</em></td>
<td class="lastpost"><em><a href="redirect.php?goto=lastpost">2026-11-15 10:0{i}</a></em><cite>by {last}</cite></td>
</tr></tbody>
"""

POSTS = [
    ("墨水瓶", "2026-11-14 09:12",
     "楼主开门见山：越狱之后的 Kindle 其实是一台 ARM Linux 小机器，"
     "只要能直接写 framebuffer，就能做出完全脱离 KOReader 的原生程序。<br/><br/>"
     "我最近在折腾一个论坛客户端，最想要的就是「整页翻页」——"
     "看长帖时不要一行行滚，而是一屏一屏翻，和读电子书一样。"
     "这里有两个难点：一是中文字形，老机型没有好用的系统字体；"
     "二是翻到论坛分页末尾时要自动把下一页接上，不能让用户点「下一页」按钮。<br/><br/>"
     "试试表情：<img src=\"images/smilies/default_smile.gif\" /> 和 "
     "<img src=\"images/smilies/default_biggrin.gif\" />，"
     "再放张图：<img src=\"attachments/photo_01.jpg\" file=\"attachment.php?aid=88\" alt=\"\" />"
     "<br/>引用一下楼下的话：<div class=\"quote\">点左侧返回，点右侧翻页，这个习惯很好记。</div>"
     "<br/>结尾来个 &amp; 符号和 &nbsp; 空格，还有 <b>加粗</b> 与 <a href=\"https://example.com\">外链文字</a>。"),
    ("老陈", "2026-11-14 10:03",
     "点左侧返回，点右侧翻页，这个习惯很好记。<br/><br/>"
     "补充一点：e-ink 的局部刷新会留残影，所以每隔几页要来一次 GC16 全刷，"
     "Kindle 原生阅读器也是这么做的。频率设成 6 页左右比较舒服。<br/>"
     "另外老设备（K3、K4）没有触摸，只有物理翻页键，输入层得同时读 EV_KEY。"
     "屏幕坐标也别硬编码，用 framebuffer 报上来的宽高和 DPI 反推字号才靠谱。"),
    ("wind", "2026-11-14 11:47",
     "GBK 编码是个隐藏大坑：论坛返回 text/html; charset=GBK，"
     "而墨水屏绘制库一般只吃 UTF-8。 Kindle 上的 gconv 模块不一定齐全，"
     "所以我把 GBK 到 Unicode 的映射表直接编进二进制，"
     "两万多条，占几十 KB，换来了零依赖。<br/><br/>"
     "字形同理，用 Unifont 的 16x16 点阵，ASCII 占一个单元、汉字占两个单元，"
     "折行按显示宽度算，中英混排不会错位。"),
    ("阿枢", "2026-11-14 14:20",
     "整理一下目前已知的限制，供后来人参考：<br/>"
     "1. 论坛必须登录才能看版块和帖子，游客会被拦在提示页；<br/>"
     "2. archiver 与 wap 目录实测都是 404，没法绕开登录；<br/>"
     "3. index.php 有 Cloudflare 的 JS 挑战，所以板块清单直接用客户端里那份 fid 表；<br/>"
     "4. Kindle 没有中文输入法，这个版本做成只读，不实现发帖和回复。<br/><br/>"
     "有账号的话，在电脑上生成 config.ini 放到 /mnt/us/hipda/ 就好，"
     "密码只存 MD5，不存明文。<img src=\"images/smilies/default_victory.gif\" />"),
]

POST_TPL = """<div id="post_{pid}">
<table id="pid{pid}" summary="post"><tr>
<td class="postauthor"><div class="postinfo"><a href="space.php?uid={uid}">{author}</a></div></td>
<td class="postcontent">
  <div class="postinfo">
    <div class="posterinfo"><div class="authorinfo"><em>{date}</em></div></div>
    <strong><a href="viewthread.php?tid=1579403&page=1#pid{pid}"><em>{floor}</em></a></strong>
  </div>
  <div class="defaultpost"><div class="postmessage"><div class="t_msgfontfix">
    <table><tr><td class="t_msgfont">{body}</td></tr></table>
  </div></div></div>
  <div class="postattachlist"><dl class="t_attachlist"><p class="attachname">
    <a href="attachment.php?aid=88">photo_01.jpg</a></p></dl></div>
</td></tr></table>
</div>
"""


def write(name: str, text: str) -> None:
    path = os.path.join(FIX, name)
    with open(path, "wb") as fh:
        fh.write(text.encode("gbk", "replace"))
    print("wrote %-32s %6d bytes (GBK)" % (name, os.path.getsize(path)))


def make_forum(page: int, count: int) -> None:
    rows = []
    start = (page - 1) * count
    for i in range(count):
        tid = 1579403 + start + i
        rows.append(TOPIC_ROW.format(
            kind="stickthread" if page == 1 and i < 2 else "normalthread",
            tid=tid, title=TITLES[(start + i) % len(TITLES)],
            attach='<img class="attach" src="images/attachicons/image_s.gif" />' if i % 5 == 0 else "",
            uid=100 + i, author=AUTHORS[i % len(AUTHORS)],
            replies=3 + i * 7, views=120 + i * 43, i=i % 10,
            last=AUTHORS[(i + 3) % len(AUTHORS)]))
    write("synthetic_forum.html" if page == 1 else "synthetic_forum_p2.html",
          HEAD.format(title="Discovery") + "".join(rows) +
          PAGER.format(cur=page, next=page + 1) + TAIL)


def make_thread(page: int, posts) -> None:
    blocks = []
    for i, (author, date, body) in enumerate(posts):
        floor = (page - 1) * 4 + i + 1
        blocks.append(POST_TPL.format(pid=90000 + floor, uid=100 + i,
                                      author=author, date=date,
                                      floor=floor, body=body))
    html = (HEAD.format(title="Kindle 上的 HiPDA 阅读体验讨论") +
            '<div id="threadtitle"><h1>Kindle 上的 HiPDA 阅读体验讨论</h1></div>' +
            '<div id="postlist">' + "".join(blocks) + "</div>" +
            THREAD_PAGER.format(cur=page) + TAIL)
    write("synthetic_thread.html" if page == 1 else "synthetic_thread_p2.html", html)


def main() -> None:
    make_forum(1, 22)
    make_forum(2, 8)
    make_thread(1, POSTS)
    make_thread(2, POSTS[:2])
    print("fixtures ready (synthetic, GBK)")


if __name__ == "__main__":
    main()
