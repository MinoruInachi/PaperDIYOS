#!/usr/bin/env python3
"""日本語 TTF を PaperDIY 用に仕立てる。

やること:
  1. バリアブルフォントを指定ウェイトで静的化する
     （Noto Sans JP のバリアブル版は既定が wght=100 = Thin なので、
       そのまま使うと本文が極端に細くなる。必ず 400 前後で固定する）
  2. 必要な文字だけに絞る（縦組み用の字形も残す）
  3. stb_truetype が読める形（グリフ輪郭は TrueType、hmtx/cmap 完備）で保存

使い方:
  python3 tools/prepare_font.py assets/NotoSansJP.ttf assets/reader-regular.ttf
  python3 tools/prepare_font.py assets/NotoSansJP.ttf assets/reader-bold.ttf --weight 700
  python3 tools/prepare_font.py 入力.ttf 出力.ttf --kanji joyo   # さらに小さく
"""
import argparse
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

# 縦組みに要る範囲。約物の縦組み用字形（U+FE10-FE19 / U+FE30-FE4F）は
# 回転描画のフォールバック先として残しておく。
BASE_RANGES = [
    (0x0020, 0x007E),  # ASCII
    (0x00A0, 0x00FF),  # Latin-1
    (0x2010, 0x2027),  # ダッシュ・引用符・リーダ
    (0x2030, 0x203B),
    (0x2190, 0x2193),  # 矢印
    (0x25A0, 0x25FF),  # 幾何学模様（箇条書き）
    (0x2600, 0x261F),
    (0x3000, 0x303F),  # CJK 記号・句読点
    (0x3040, 0x309F),  # ひらがな
    (0x30A0, 0x30FF),  # カタカナ
    (0x31F0, 0x31FF),  # カタカナ拡張
    (0x3200, 0x33FF),  # 囲み文字・単位
    (0xFE10, 0xFE19),  # 縦組み約物
    (0xFE30, 0xFE4F),  # 縦組み括弧・罫
    (0xFF01, 0xFF60),  # 全角英数・記号
    (0xFFE0, 0xFFE6),  # 全角通貨
]

KANJI_SETS = {
    "all": [(0x3400, 0x4DBF), (0x4E00, 0x9FFF), (0xF900, 0xFAFF)],
    "cjk": [(0x4E00, 0x9FFF), (0xF900, 0xFAFF)],
    "joyo": None,  # 下の JOYO_EXTRA と常用漢字表から作る
}

# 常用漢字 2136 字（2010 年内閣告示）。
JOYO = (
    "亜哀挨愛曖悪握圧扱宛嵐安案暗以衣位囲医依委威為畏胃尉異移萎偉椅彙意違維慰"
    "遺緯域育一壱逸茨芋引印因咽姻員院淫陰飲隠韻右宇羽雨唄鬱畝浦運雲永泳英映栄"
    "営詠影鋭衛易疫益液駅悦越謁閲円延沿炎怨宴媛援園煙猿遠鉛塩演縁艶汚王凹央応"
    "往押旺欧殴桜翁奥横岡屋億憶臆虞乙俺卸音恩温穏下化火加可仮何花佳価果河苛科"
    "架夏家荷華菓貨渦過嫁暇禍靴寡歌箇稼課蚊牙瓦我画芽賀雅餓介回灰会快戒改怪拐"
    "悔海界皆械絵開階塊楷解潰壊懐諧貝外劾害崖涯街慨蓋該概骸垣柿各角拡革格核殻"
    "郭覚較隔閣確獲嚇穫学岳楽額顎掛潟括活喝渇割葛滑褐轄且株釜鎌刈甘汗缶完肝官"
    "冠巻看陥乾勘患貫寒喚堪換敢棺款間閑勧寛幹感漢慣管関歓監緩憾還館環簡観韓艦"
    "鑑丸含岸岩玩眼頑顔願企伊危机気岐希忌汽奇祈季紀軌既記起飢鬼帰基寄規亀喜幾"
    "揮期棋貴棄毀旗器畿輝機騎技宜偽欺義疑儀戯擬犠議菊吉喫詰却客脚逆虐九久及弓"
    "丘旧休吸朽臼求究泣急級糾宮救球給嗅窮牛去巨居拒拠挙虚許距魚御漁凶共叫狂京"
    "享供協況峡挟狭恐恭胸脅強教郷境橋矯鏡競響驚仰暁業凝曲局極玉巾斤均近金菌勤"
    "琴筋僅禁緊錦謹襟吟銀区句苦駆具惧愚空偶遇隅串屈掘窟熊繰君訓勲薫軍郡群兄刑"
    "形系径茎係型契計恵啓掲渓経蛍敬景軽傾携継詣慶憬稽憩警鶏芸迎鯨隙劇撃激桁欠"
    "穴血決結傑潔月犬件見券肩建研県倹兼剣拳軒健険圏堅検嫌献絹遣権憲賢謙鍵繭顕"
    "験懸元幻玄言弦限原現舷減源厳己戸古呼固股虎孤弧故枯個庫湖雇誇鼓錮顧五互午"
    "呉後娯悟碁語誤護口工公勾孔功巧広甲交光向后好江考行坑孝抗攻更効幸拘肯侯厚"
    "恒洪皇紅荒郊香候校耕航貢降高康控梗黄喉慌港硬絞項溝鋼稿興衡鋼講購乞号合拷"
    "剛傲豪克告谷刻国黒穀酷獄骨駒込頃今困昆恨根婚混痕紺魂墾懇左佐沙査砂唆差詐"
    "鎖座挫才再災妻采砕宰栽彩採済祭斎細菜最裁債催塞歳載際埼在材剤財罪崎作削昨"
    "柵索策酢搾錯咲冊札刷刹拶殺察撮擦雑皿三山参桟蚕惨産傘散算酸賛残斬暫士子支"
    "止氏仕史司四市矢旨死糸至伺志私使刺始姉枝祉肢姿思指施師恣紙脂視紫詞歯嗣試"
    "詩資飼誌雌摯賜諮示字寺次耳自似児事侍治持時滋慈辞磁餌璽鹿式識軸七叱失室疾"
    "執湿嫉漆質実芝写社車舎者射捨赦斜煮遮謝邪蛇尺借酌釈爵若弱寂手主守朱取狩首"
    "殊珠酒腫種趣寿受呪授需儒樹収囚州舟秀周宗拾秋臭修袖終羞習週就衆集愁酬醜蹴"
    "襲十汁充住柔重従渋銃獣縦叔祝宿淑粛縮塾熟出述術俊春瞬旬巡盾准殉純循順準潤"
    "遵処初所書庶暑署緒諸女如助序叙徐除小升少召匠床抄肖尚招承昇松沼昭宵将消症"
    "祥称笑唱商渉章紹訟勝掌晶焼焦硝粧詔証象傷奨照詳彰障憧衝賞償礁鐘上丈冗条状"
    "乗城浄剰常情場畳蒸縄壌嬢錠譲醸色拭食植殖飾触嘱織職辱尻心申伸臣芯身辛侵信"
    "津神唇娠振浸真針深紳進森診寝慎新審震薪親人刃仁尽迅甚陣尋腎須図水吹垂炊帥"
    "粋衰推酔遂睡穂随髄枢崇数据杉裾寸瀬是井世正生成西声制姓征性青斉政星牲省凄"
    "逝清盛婿晴勢聖誠精製誓静請整醒税夕斥石赤昔析席脊隻惜戚責跡積績籍切折拙窃"
    "接設雪摂節説舌絶千川仙占先宣専泉浅洗染扇栓旋船戦煎羨腺詮践箋銭潜線遷選薦"
    "繊鮮全前善然禅漸膳繕狙阻祖租素措粗組疎訴塑遡礎双壮早争走奏相荘草送倉捜挿"
    "桑巣掃曹曽爽窓創喪痩葬装僧想層総遭槽踪操燥霜騒藻造像増憎蔵贈臓即束足促則"
    "息捉速側測俗族属賊続卒率存村孫尊損遜他多汰打妥唾堕惰駄太対体耐待怠胎退帯"
    "泰堆袋逮替貸隊滞態戴大代台第題滝宅択沢卓拓託濯諾濁但達脱奪棚誰丹旦担単炭"
    "胆探淡短嘆端綻誕鍛団男段断弾暖談壇地池知値恥致遅痴稚置緻竹畜逐蓄築秩窒茶"
    "着嫡中仲虫沖宙忠抽注昼柱衷酎鋳駐著貯丁弔庁兆町長挑帳張彫眺釣頂鳥朝貼超腸"
    "跳徴嘲潮澄調聴懲直勅捗沈珍朕陳賃鎮追椎墜通痛塚漬坪爪鶴低呈廷弟定底抵邸亭"
    "貞帝訂庭逓停偵堤提程艇締諦泥的笛摘滴適敵溺迭哲鉄徹撤天典店点展添転填田伝"
    "殿電斗吐妬徒途都渡塗賭土奴努度怒刀冬灯当投豆東到逃倒凍唐島桃討透党悼盗陶"
    "塔搭棟湯痘登答等筒統稲踏糖頭謄藤闘騰同洞胴動堂童道働銅導瞳峠匿特得督徳篤"
    "毒独読栃凸突届屯豚頓貪鈍曇丼那奈内梨謎鍋南軟難二尼弐匂肉虹日入乳尿任妊忍"
    "認寧熱年念捻粘燃悩納能脳農濃把波派破覇馬婆罵拝杯背肺俳配排敗廃輩売倍梅培"
    "陪媒買賠白伯拍泊迫剥舶博薄麦漠縛爆箱箸畑肌八鉢発髪伐抜罰閥反半氾犯帆汎伴"
    "判坂阪板版班畔般販斑飯搬煩頒範繁藩晩番蛮盤比皮妃否批彼披肥非卑飛疲秘被悲"
    "扉費碑罷避尾眉美備微鼻膝肘匹必泌筆姫百氷表俵票評漂標苗秒病描猫品浜貧賓頻"
    "敏瓶不夫父付布婦怖阜附訃負赴浮婦符富普腐敷膚賦譜侮武部舞封風伏服副幅復福"
    "腹複覆払沸仏物粉紛雰噴墳憤奮分文聞丙平兵併並柄陛閉塀幣弊蔽餅米壁璧癖別蔑"
    "片辺返変偏遍編弁便勉歩保哺捕補舗母募墓慕暮簿方包芳邦奉宝抱放法泡胞俸倣峰"
    "砲崩訪報蜂豊飽褒縫亡乏忙坊妨忘防房肪某冒剖紡望傍帽棒貿貌暴膨謀頬北木朴牧"
    "睦僕墨撲没勃堀本奔翻凡盆麻摩磨魔毎妹枚昧埋幕膜枕又末抹万満慢漫未味魅巳密"
    "蜜脈妙民眠矛務無夢霧娘名命明迷冥盟銘鳴滅免面綿麺茂模毛妄盲耗猛網目黙門紋"
    "問冶夜野弥厄役約訳薬躍闇由油喩愉諭輸癒唯友有勇幽悠郵湧猶裕遊雄誘憂融優与"
    "予余誉預幼用羊妖洋要容庸揚揺葉陽溶腰様瘍踊窯養擁謡曜抑沃浴欲翌翼拉裸羅来"
    "雷頼絡落酪辣乱卵覧濫藍欄吏利里理痢裏履璃離陸立律慄略柳流留竜粒隆硫侶旅虜"
    "慮了両良料涼猟陵量僚領寮療瞭糧力緑林厘倫輪隣臨瑠涙累塁類令礼冷励戻例鈴零"
    "霊隷齢麗暦歴列劣烈裂恋連廉練錬呂炉賂路露老労弄郎朗浪廊楼漏籠六録麓論和話"
    "賄脇惑枠湾腕"
)


def build_unicodes(kanji_set: str, extra: str) -> set:
    codes = set()
    for lo, hi in BASE_RANGES:
        codes.update(range(lo, hi + 1))
    if kanji_set == "joyo":
        codes.update(ord(c) for c in JOYO)
        # 人名・地名で頻出する常用外の漢字を少し足す
        codes.update(ord(c) for c in "々〆ヶ丑寅卯辰巳未申酉戌亥彦姫嶋﨑瀧祐禎彌")
    else:
        for lo, hi in KANJI_SETS[kanji_set]:
            codes.update(range(lo, hi + 1))
    codes.update(ord(c) for c in extra)
    return codes


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--weight", type=float, default=400.0,
                    help="バリアブルフォントを固定するウェイト（既定 400）")
    ap.add_argument("--kanji", choices=list(KANJI_SETS), default="cjk",
                    help="収録する漢字の範囲（既定 cjk = 統合漢字全部）")
    ap.add_argument("--extra", default="", help="追加で収録する文字をそのまま並べる")
    ap.add_argument("--no-subset", action="store_true", help="文字を絞らない")
    args = ap.parse_args()

    font = TTFont(args.src)
    if "fvar" in font:
        axes = {a.axisTag: (a.minValue, a.defaultValue, a.maxValue) for a in font["fvar"].axes}
        loc = {}
        if "wght" in axes:
            lo, _, hi = axes["wght"]
            loc["wght"] = max(lo, min(hi, args.weight))
        for tag, (lo, default, hi) in axes.items():
            if tag not in loc:
                loc[tag] = default
        print(f"バリアブルフォントを固定: {loc}")
        font = instancer.instantiateVariableFont(font, loc, inplace=False, updateFontNames=True)

    if not args.no_subset:
        codes = build_unicodes(args.kanji, args.extra)
        have = set(font.getBestCmap())
        keep = sorted(codes & have)
        print(f"収録する文字: {len(keep)} 字（要求 {len(codes)} 字のうちフォントにあるもの）")
        opts = subset.Options()
        opts.layout_features = ["*"]
        opts.name_IDs = ["*"]
        opts.notdef_outline = True
        opts.recalc_bounds = True
        opts.drop_tables += ["DSIG"]
        subsetter = subset.Subsetter(options=opts)
        subsetter.populate(unicodes=keep)
        subsetter.subset(font)

    font.flavor = None
    font.save(args.dst)

    import os
    size = os.path.getsize(args.dst)
    out = TTFont(args.dst)
    print(f"出力: {args.dst}  {size/1024/1024:.2f} MB  グリフ {out['maxp'].numGlyphs} 字"
          f"  unitsPerEm {out['head'].unitsPerEm}")
    if "glyf" not in out:
        print("警告: TrueType 輪郭 (glyf) がありません。stb_truetype は CFF も読めますが、"
              "TrueType 輪郭のフォントのほうが確実です。", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
