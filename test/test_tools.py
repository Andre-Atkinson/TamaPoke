#!/usr/bin/env python3
"""Tests de las herramientas del taller (tools/) y de los ficheros generados.

  python3 test/test_tools.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, 'tools')
sys.path.insert(0, TOOLS)

# The firmware's own sources in the sketch folder: TamaPoke.ino and the modules split
# out of it (#41), with the rest of the sketch root. Left out: the generated dex.h and
# species.h, and the translations in i18n.cpp, whose literals are UTF-8 on purpose.
SKETCH_SKIP = {'dex.h', 'species.h', 'i18n.cpp'}


def sketch_sources():
    """{name: text} of every sketch-root .ino/.cpp/.h except SKETCH_SKIP."""
    out = {}
    for name in sorted(os.listdir(ROOT)):
        if name.endswith(('.ino', '.cpp', '.h')) and name not in SKETCH_SKIP:
            with open(os.path.join(ROOT, name), encoding='utf-8') as fh:
                out[name] = fh.read()
    return out


class TestToolsCompile(unittest.TestCase):
    def test_todos_los_scripts_compilan(self):
        scripts = sorted(f for f in os.listdir(TOOLS) if f.endswith('.py'))
        self.assertTrue(scripts, 'no se encontro ningun script en tools/')
        for name in scripts:
            with self.subTest(script=name):
                with open(os.path.join(TOOLS, name), encoding='utf-8') as fh:
                    src = fh.read()
                try:
                    compile(src, name, 'exec')
                except SyntaxError as e:
                    self.fail(f'{name} no compila: {e}')

    def test_los_scripts_ejecutables_declaran_shebang(self):
        for name in sorted(f for f in os.listdir(TOOLS) if f.endswith('.py')):
            path = os.path.join(TOOLS, name)
            if not os.access(path, os.X_OK):
                continue
            with open(path) as fh:
                self.assertTrue(fh.readline().startswith('#!'),
                                f'{name} es ejecutable pero no tiene shebang')


class TestDexData(unittest.TestCase):
    """Invariantes de la fuente de datos de la Pokedex."""

    @classmethod
    def setUpClass(cls):
        from dex_data import DEX, TYPE_ACCENTS, CLASSIC, RARE, LEGENDARY, SLUGS
        from dex_stats import BASE_STATS
        cls.DEX, cls.ACCENTS, cls.CLASSIC = DEX, TYPE_ACCENTS, CLASSIC
        cls.RARE, cls.LEGENDARY, cls.SLUGS = RARE, LEGENDARY, SLUGS
        cls.STATS = BASE_STATS
        cls.byNum = {row[0]: row for row in DEX}

    def test_estan_las_151_especies_sin_huecos_ni_repetidos(self):
        nums = [row[0] for row in self.DEX]
        self.assertEqual(len(nums), 151)
        self.assertEqual(sorted(nums), list(range(1, 152)))

    def test_los_slugs_son_unicos_y_en_minusculas(self):
        slugs = [row[1] for row in self.DEX]
        self.assertEqual(len(set(slugs)), len(slugs), 'hay slugs repetidos')
        for s in slugs:
            self.assertEqual(s, s.lower(), f'slug con mayusculas: {s}')
            self.assertTrue(s.isascii(), f'slug no ASCII: {s}')

    def test_los_nombres_de_pantalla_son_ascii_en_mayusculas(self):
        # la fuente GFX del firmware no tiene acentos ni minusculas
        for num, slug, display, *_ in self.DEX:
            self.assertTrue(display.isascii(), f'{num} {display} no es ASCII')
            self.assertEqual(display, display.upper(), f'{num} {display} lleva minusculas')
            self.assertLessEqual(len(display), 12, f'{num} {display} no cabe en pantalla')

    def test_las_evoluciones_apuntan_a_especies_reales(self):
        for num, slug, display, tipo, evo, lvl in self.DEX:
            if evo:
                self.assertIn(evo, self.byNum, f'{display} evoluciona a {evo}, que no existe')
                self.assertNotEqual(evo, num, f'{display} evoluciona a si mismo')
                self.assertGreater(lvl, 0, f'{display} evoluciona sin nivel')
            else:
                self.assertEqual(lvl, 0, f'{display} es forma final pero tiene nivel {lvl}')

    def test_ninguna_cadena_evolutiva_hace_bucle(self):
        for num in self.byNum:
            seen, cur = set(), num
            while cur and self.byNum[cur][4]:
                self.assertNotIn(cur, seen, f'bucle evolutivo desde {num}')
                seen.add(cur)
                cur = self.byNum[cur][4]
                self.assertLessEqual(len(seen), 5, f'cadena demasiado larga desde {num}')

    def test_todos_los_tipos_tienen_color(self):
        for num, slug, display, tipo, *_ in self.DEX:
            self.assertIn(tipo, self.ACCENTS, f'{display}: tipo "{tipo}" sin color')

    def test_los_colores_son_hex_de_6_digitos(self):
        for tipo, col in self.ACCENTS.items():
            self.assertRegex(col, r'^#[0-9a-fA-F]{6}$', f'color raro en {tipo}: {col}')

    def test_las_rarezas_apuntan_a_formas_base(self):
        evolucionadas = {row[4] for row in self.DEX if row[4]}
        for num in sorted(self.RARE | self.LEGENDARY):
            self.assertIn(num, self.byNum, f'rareza para una especie inexistente: {num}')
            self.assertNotIn(num, evolucionadas,
                             f'{self.byNum[num][2]} solo sale por evolucion: la rareza no le sirve')
        self.assertFalse(self.RARE & self.LEGENDARY, 'una especie no puede ser rara y legendaria')

    def test_los_iniciales_clasicos_existen(self):
        evolucionadas = {row[4] for row in self.DEX if row[4]}
        for num in self.CLASSIC:
            self.assertIn(num, self.byNum, f'inicial inexistente: {num}')
            self.assertNotIn(num, evolucionadas, f'{self.byNum[num][2]} no puede ser inicial')

    def test_hay_stats_base_de_las_151(self):
        for num in self.byNum:
            self.assertIn(num, self.STATS, f'faltan stats de {self.byNum[num][2]}')
            hp, atk, dfn, spe = self.STATS[num]
            for v in (hp, atk, dfn, spe):
                self.assertTrue(0 < v <= 255, f'stat fuera de rango en {self.byNum[num][2]}: {v}')


class TestGeneratedFiles(unittest.TestCase):
    """dex.h esta generado: si no coincide con su generador, alguien lo edito a mano."""

    def test_gen_dex_reproduce_dex_h(self):
        tmp = tempfile.mkdtemp(prefix='tamapoke-gen-')
        try:
            shutil.copytree(TOOLS, os.path.join(tmp, 'tools'))
            r = subprocess.run([sys.executable, os.path.join(tmp, 'tools', 'gen_dex.py')],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, f'gen_dex.py fallo:\n{r.stderr}')
            generated = os.path.join(tmp, 'dex.h')
            self.assertTrue(os.path.exists(generated), 'gen_dex.py no escribio dex.h')
            with open(generated) as a, open(os.path.join(ROOT, 'dex.h')) as b:
                self.assertEqual(a.read(), b.read(),
                                 'dex.h no coincide con lo que genera tools/gen_dex.py '
                                 '(vuelve a generarlo en vez de editarlo a mano)')
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_los_ficheros_generados_avisan_de_que_lo_son(self):
        for name in ('dex.h', 'species.h'):
            with open(os.path.join(ROOT, name)) as fh:
                head = fh.read(400)
            self.assertIn('GENERADO', head, f'{name} deberia decir que esta generado')


class TestSketchSanity(unittest.TestCase):
    """Comprobaciones baratas sobre el sketch que no necesitan compilarlo."""

    @classmethod
    def setUpClass(cls):
        cls.sources = sketch_sources()

    def test_las_fuentes_del_sketch_incluyen_el_ino(self):
        self.assertIn('TamaPoke.ino', self.sources)

    def test_el_sketch_no_lleva_caracteres_no_ascii_en_literales_de_pantalla(self):
        # los comentarios pueden llevar lo que quieran; los literales, no:
        # la fuente GFX es ASCII y un acento sale como basura
        for name, text in self.sources.items():
            for lineno, line in enumerate(text.splitlines(), 1):
                code = line.split('//')[0]
                for chunk in code.split('"')[1::2]:
                    self.assertTrue(chunk.isascii(),
                                    f'{name}:{lineno}: literal no ASCII: {chunk!r}')

    def test_las_llaves_estan_equilibradas(self):
        for name, text in self.sources.items():
            self.assertEqual(text.count('{'), text.count('}'),
                             f'llaves descompensadas en {name}')



def pt_glyph_codes():
    """The bytes ptglyphs.h draws (#61), in table order."""
    with open(os.path.join(ROOT, 'ptglyphs.h'), encoding='utf-8') as fh:
        text = fh.read()
    table = re.search(r'PT_GLYPHS\[\]\s*=\s*\{(.*?)\n\};', text, re.S).group(1)
    return [int(c, 16) for c in re.findall(r'\{0x([0-9A-Fa-f]{2}),\s*\{', table)]


class TestPtGlyphs(unittest.TestCase):
    """ptglyphs.h (#61): the extra glyphs are exactly the European Portuguese letters the
    classic font (a CP437 table) lacks, each on its Latin-1 byte, so "\\303" in a string
    is the letter a Latin-1 table says it is; plus the CP437 letters it only redraws (#85:
    E-acute, so the accented capitals match), each on its CP437 byte."""

    REDRAWN = 'É'

    def test_the_glyphs_are_the_letters_cp437_lacks_and_the_redrawn_ones(self):
        portuguese = 'áàâãçéêíóôõúÁÀÂÃÇÉÊÍÓÔÕÚ'

        def in_cp437(ch):
            try:
                ch.encode('cp437')
                return True
            except UnicodeEncodeError:
                return False

        codes = pt_glyph_codes()
        self.assertEqual(len(codes), len(set(codes)), 'a glyph is listed twice')
        # ord() of these letters is both their code point and their Latin-1 byte; a redrawn
        # letter keeps its CP437 byte
        want = [ord(ch) for ch in portuguese if not in_cp437(ch)]
        want += [ch.encode('cp437')[0] for ch in self.REDRAWN]
        self.assertEqual(sorted(codes), sorted(want))


def strip_c_comments(text):
    """text with // and /* */ comments blanked (newlines kept), string literals left alone."""
    return re.sub(r'//[^\n]*|/\*.*?\*/|("(?:[^"\\\n]|\\.)*")',
                  lambda m: m.group(1) or re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)


def call_args(text, open_paren):
    """the top-level arguments of the call whose '(' is at text[open_paren]; commas inside
    nested parentheses (C565(...), RGB macros) and string literals are not counted."""
    depth, args, cur, i = 0, [], '', open_paren
    while i < len(text):
        ch = text[i]
        if ch == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            cur += text[i:j + 1]
            i = j + 1
            continue
        if ch in '([{':
            depth += 1
            if depth > 1:
                cur += ch
        elif ch in ')]}':
            depth -= 1
            if depth == 0:
                args.append(cur.strip())
                return args
            cur += ch
        elif ch == ',' and depth == 1:
            args.append(cur.strip())
            cur = ''
        else:
            cur += ch
        i += 1
    raise ValueError('unbalanced parentheses')


class TestTextColorHasNoBackground(unittest.TestCase):
    """uitext.cpp draws the ptglyphs.h letters through Arduino_GFX's GFXfont path, which
    with a background colour fills a box starting baseline * size above the cursor: behind
    those letters only, and misplaced. So every setTextColor() takes one argument."""

    def test_every_setTextColor_call_has_one_argument(self):
        calls = 0
        for name, text in sketch_sources().items():
            code = strip_c_comments(text)
            for m in re.finditer(r'\bsetTextColor\s*\(', code):
                line = code.count('\n', 0, m.start()) + 1
                args = call_args(code, m.end() - 1)
                calls += 1
                with self.subTest(file=name, line=line):
                    self.assertEqual(len(args), 1,
                                     f'{name}:{line}: setTextColor({", ".join(args)}) sets a background')
        self.assertGreaterEqual(calls, 20, 'the pattern no longer finds the setTextColor() calls')

    def test_the_argument_counter_handles_nested_commas(self):
        self.assertEqual(call_args('f(C565(1, 2, 3))', 1), ['C565(1, 2, 3)'])
        self.assertEqual(call_args('f(a, b)', 1), ['a', 'b'])
        self.assertEqual(call_args('f(g(a), "x,y")', 1), ['g(a)', '"x,y"'])


class TestFarewellButtonWidth(unittest.TestCase):
    """test_i18n.cpp cannot include app.h, so it carries its own FAR_BTN_W (TEST_FAR_BTN_W)."""

    def test_the_test_copy_of_FAR_BTN_W_matches_app_h(self):
        with open(os.path.join(ROOT, 'app.h'), encoding='utf-8') as fh:
            app = int(re.search(r'#define FAR_BTN_W (\d+)', fh.read()).group(1))
        with open(os.path.join(ROOT, 'test', 'test_i18n.cpp'), encoding='utf-8') as fh:
            mirror = int(re.search(r'#define TEST_FAR_BTN_W (\d+)', fh.read()).group(1))
        self.assertEqual(mirror, app, 'update TEST_FAR_BTN_W in test/test_i18n.cpp')


class TestOwnerLetters(unittest.TestCase):
    """owner.h's accented letters (#23, #61) against Python's own CP437 codec and Unicode data.

    The name's letters must be exactly the ones the UI font draws: every CP437 byte
    0x80-0xFF that is one ASCII letter plus one accent, on that byte, and the 12
    Portuguese letters of ptglyphs.h, on their Latin-1 byte (= their code point). A CP437
    letter ptglyphs.h only redraws (#85: E-acute) stays on its CP437 byte."""

    def test_the_table_is_the_fonts_accented_letters(self):
        import unicodedata
        with open(os.path.join(ROOT, 'owner.h'), encoding='utf-8') as fh:
            text = fh.read()
        table = re.search(r'OWNER_LETTERS\[\]\s*=\s*\{(.*?)\};', text, re.S).group(1)
        rows = [(int(cp, 16), int(b, 16)) for cp, b in
                re.findall(r"\{0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\}", table)]
        self.assertTrue(rows)
        want = {}
        for b in range(0x80, 0x100):
            ch = bytes([b]).decode('cp437')
            d = unicodedata.normalize('NFD', ch)
            if unicodedata.category(ch).startswith('L') and len(d) == 2 and d[0].isascii() and d[0].isalpha():
                want[ord(ch)] = b
        redrawn = {ch.encode('cp437')[0] for ch in TestPtGlyphs.REDRAWN}
        for code in pt_glyph_codes():
            if code in redrawn:
                self.assertIn(code, want.values(), 'a redrawn letter that CP437 lacks')
                continue
            self.assertNotIn(code, want, 'a ptglyphs.h letter that CP437 already has')
            want[code] = code
        self.assertEqual(dict(rows), want)
        self.assertEqual(len(rows), len(want), 'a letter is listed twice')


class TestToastsAreTranslated(unittest.TestCase):
    """#61: the 1.75C toasts the player sees go through T(), so they follow the language.

    showControlToast() takes the text to show; a string literal there is English on a
    Portuguese board. The only literal allowed is the developer message of the training
    capture (TRAINTRACE), which a player never triggers."""

    DEVELOPER = {'Touch capture unavailable'}

    def test_every_toast_is_a_translated_string(self):
        calls = 0
        for name, text in sketch_sources().items():
            for n, line in enumerate(text.splitlines(), 1):
                code = line.split('//')[0]
                if 'showControlToast(' not in code or re.search(r'\bvoid\s+showControlToast\(', code):
                    continue
                calls += 1
                for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', code.split('showControlToast(', 1)[1]):
                    with self.subTest(file=name, line=n):
                        self.assertIn(lit, self.DEVELOPER, f'{name}:{n}: an untranslated toast "{lit}"')
        self.assertGreaterEqual(calls, 3, 'the pattern no longer finds the showControlToast() calls')

    def test_the_lock_hint_is_translated(self):
        ino = sketch_sources()['TamaPoke.ino']
        self.assertRegex(ino, r'const char \*hint = lockHint \? T\(S_TOAST_LOCKED\)')


class TestBuffersDeTexto(unittest.TestCase):
    """Cada snprintf(buf, sizeof(buf), T(S_x), ...) cabe en su buffer en TODOS los idiomas.

    snprintf corta en silencio: se pierden cifras o el final de un nombre, y no
    lo avisa ni el compilador ni la pantalla (con el japones y el coreano, en
    UTF-8, llego a pasar: "きろく 2" por "きろく 219"). Se comprueba aqui con el
    peor caso de cada argumento segun su tipo:

      %u / %d  -> 5 cifras (todos los argumentos son de 16 bits o menos)
      %lu      -> 10 cifras
      %s       -> lo que declare ARGS_S; un %s sin declarar hace fallar el
                  test, para que quien lo anada piense cuanto puede ocupar
                  ('dueno': the player's name in the font's encoding, one
                  byte per character, OWNER_NAME_MAX in owner.h)
    """

    ARGS_S = {  # que puede llegar por cada %s, en orden
        'S_NAME_FMT': ('estrella', 'nombre_o_apodo'),
        'S_RELEASE_FMT': ('nombre',),
        'S_INFO_FMT': ('baya',),
        'S_NATURE_FMT': ('naturaleza',),
        'S_FAREWELL_BTN': ('nombre_o_apodo',),
        'S_RUNAWAY_BTN': ('nombre_o_apodo',),
        'S_SAY_LOVE_FMT': ('dueno',),
    }
    CIFRAS = {'u': 5, 'd': 5, 'lu': 10}
    ESPEC = re.compile(r'%0?\d*(lu|u|d|s)')

    @classmethod
    def setUpClass(cls):
        import test_i18n_formats as fmt
        from dex_data import DEX

        def leer(nombre):
            with open(os.path.join(ROOT, nombre), encoding='utf-8') as fh:
                return fh.read()

        cls.LANGS = fmt.LANGS
        cuerpo = re.search(r'enum\s+StrId[^{]*\{(.*?)\}', leer('i18n.h'), re.S).group(1)
        ids = re.findall(r'\b(S_[A-Z0-9_]+)\b', cuerpo)
        filas = fmt.extract_table(leer('i18n.cpp'), 'STRINGS', len(cls.LANGS))
        cls.LIT = {sid: [fila[k] for fila in filas] for k, sid in enumerate(ids)}
        cls.sources = {name: text.splitlines() for name, text in sketch_sources().items()}
        cls.APODO = int(re.search(r'char\s+nick\[(\d+)\]', leer('pet.h')).group(1)) - 1
        cls.DUENO = int(re.search(r'#define\s+OWNER_NAME_MAX\s+(\d+)', leer('owner.h')).group(1))
        # both languages use the English species names (#61)
        base = max(len(row[2].encode()) for row in DEX)
        cls.NOMBRE = {lang: base for lang in cls.LANGS}

    @staticmethod
    def bytes_literal(literal):
        """Bytes que ocupa en memoria un literal C escrito como en el fuente."""
        cuerpo = literal[1:-1].encode('utf-8')
        n = i = 0
        while i < len(cuerpo):
            if cuerpo[i] == 0x5C:  # barra invertida: \ooo octal, o \n \" \\ ...
                j = i + 1
                while j < len(cuerpo) and j < i + 4 and 0x30 <= cuerpo[j] <= 0x37:
                    j += 1
                i = j if j > i + 1 else i + 2
            else:
                i += 1
            n += 1
        return n

    def peor_caso(self, sid, li):
        lang, lit = self.LANGS[li], self.LIT[sid][li]
        total = self.bytes_literal(lit) + 1  # + terminador
        pendientes = list(self.ARGS_S.get(sid, ()))
        for m in self.ESPEC.finditer(lit):
            total -= len(m.group(0))
            if m.group(1) != 's':
                total += self.CIFRAS[m.group(1)]
                continue
            self.assertTrue(pendientes, f'{sid}: tiene un %s sin declarar en ARGS_S')
            arg = pendientes.pop(0)
            if arg == 'estrella':
                total += 1
            elif arg == 'nombre':
                total += self.NOMBRE[lang]
            elif arg == 'nombre_o_apodo':
                total += max(self.NOMBRE[lang], self.APODO)
            elif arg == 'dueno':
                total += self.DUENO
            elif arg == 'baya':
                total += max(self.bytes_literal(self.LIT[b][li]) for b in
                             ('S_BERRY_UNK', 'S_BERRY_RED', 'S_BERRY_BLUE', 'S_BERRY_GREEN'))
            elif arg == 'naturaleza':  # natureName() (#25)
                total += max(self.bytes_literal(self.LIT[n][li]) for n in
                             ('S_NAT_EASYGOING', 'S_NAT_GREEDY', 'S_NAT_PLAYFUL', 'S_NAT_TIDY', 'S_NAT_LAZY'))
            else:
                self.fail(f'{sid}: ARGS_S names {arg!r}, which peor_caso() does not know')
        return total

    def test_cada_texto_formateado_cabe_en_su_buffer_en_todos_los_idiomas(self):
        llamada = re.compile(r'snprintf\((\w+),\s*sizeof\(\1\),\s*T\((S_[A-Z0-9_]+)\)')
        revisadas = 0
        for name, lineas in self.sources.items():
            for n, linea in enumerate(lineas, 1):
                m = llamada.search(linea.split('//')[0])
                if not m:
                    continue
                var, sid = m.groups()
                tam = next((int(d.group(1)) for k in range(n - 2, -1, -1)
                            for d in [re.search(r'\bchar\s+' + var + r'\s*\[(\d+)\]', lineas[k])]
                            if d), None)
                self.assertIsNotNone(tam, f'{name}:{n}: no encuentro la declaracion de {var}')
                revisadas += 1
                for li, lang in enumerate(self.LANGS):
                    with self.subTest(fichero=name, linea=n, id=sid, idioma=lang):
                        peor = self.peor_caso(sid, li)
                        self.assertLessEqual(
                            peor, tam,
                            f'{name}:{n}: {var}[{tam}] con {sid} en {lang} necesita {peor} bytes')
        self.assertGreater(revisadas, 10, 'el patron de snprintf ya no encuentra las llamadas')


def put(path, data=b''):
    with open(path, 'wb') as fh:
        fh.write(data)


class TestBoardScripts(unittest.TestCase):
    """flash_175c.sh / pet_save.sh / board_env.sh against a fake esptool that
    only records its arguments, so nothing here touches a real board."""

    NVS_BYTES = 0x5000

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.log = os.path.join(self.tmp, 'esptool.log')
        self.fake = os.path.join(self.tmp, 'esptool')
        # Records each call; for read-flash it writes SIZE-sized zeros to the
        # destination, like the real tool would. FAKE_READ=fail makes read-flash
        # exit 2; FAKE_READ=short writes 4096 bytes fewer than asked.
        with open(self.fake, 'w') as fh:
            fh.write('#!/usr/bin/env bash\n'
                     f'echo "$*" >> "{self.log}"\n'
                     'args=("$@")\n'
                     'for i in "${!args[@]}"; do\n'
                     '  if [[ "${args[$i]}" == read-flash ]]; then\n'
                     '    [[ "${FAKE_READ:-}" == fail ]] && exit 2\n'
                     '    size=$(( ${args[$((i+2))]} )); dest="${args[$((i+3))]}"\n'
                     '    [[ "${FAKE_READ:-}" == short ]] && size=$((size - 4096))\n'
                     '    head -c "$size" /dev/zero > "$dest"\n'
                     '  fi\n'
                     'done\n')
        os.chmod(self.fake, 0o755)
        self.port = os.path.join(self.tmp, 'fake-port')  # not a tty: board.py fails, as intended
        open(self.port, 'w').close()
        self.env = dict(os.environ, ESPTOOL=self.fake,
                        TAMAPOKE_BACKUP_DIR=os.path.join(self.tmp, 'backups'))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_tool(self, *args):
        return subprocess.run([os.path.join(TOOLS, args[0]), *args[1:]], env=self.env,
                              capture_output=True, text=True, timeout=60)

    def make_save(self, name='pet.nvs.bin', sha=True, first_word=b'\xfe\xff\xff\xff'):
        """A 20 KB NVS-shaped file: page 0 active, the rest erased."""
        path = os.path.join(self.tmp, name)
        data = bytearray(b'\xff' * self.NVS_BYTES)
        data[0:4] = first_word
        with open(path, 'wb') as fh:
            fh.write(data)
        if sha:
            import hashlib
            with open(path + '.sha256', 'w') as fh:
                fh.write(hashlib.sha256(data).hexdigest() + '  ' + name + '\n')
        return path

    def calls(self):
        if not os.path.exists(self.log):
            return []
        with open(self.log) as fh:
            return [line.split() for line in fh if line.strip()]

    def test_partition_offsets_come_from_the_175c_table(self):
        out = subprocess.run(
            ['bash', '-c', f'source "{TOOLS}/board_env.sh"; part_off nvs; part_size nvs; '
                           'part_off app0; part_off storage'],
            capture_output=True, text=True, check=True).stdout.split()
        self.assertEqual(out, ['0x9000', '0x5000', '0x10000', '0x310000'])

    def test_app_only_writes_just_app0(self):
        outdir = os.path.join(self.tmp, 'out')
        os.makedirs(outdir)
        put(os.path.join(outdir, 'TamaPoke.ino.bin'))
        r = self.run_tool('flash_175c.sh', '--port', self.port, '--out', outdir, '--app-only')
        self.assertEqual(r.returncode, 0, r.stderr)
        (call,) = self.calls()
        addrs = [a for a in call if a.startswith('0x')]
        self.assertEqual(addrs, ['0x10000'], f'app-only must not write anything else: {call}')
        for form in ('write-flash', '--flash-mode', '--flash-freq', '--flash-size'):
            self.assertIn(form, call, 'esptool 5 spelling expected (the underscore forms are deprecated)')

    def test_restore_refuses_a_file_that_is_not_a_save(self):
        bad = os.path.join(self.tmp, 'not-a-save.bin')
        with open(bad, 'wb') as fh:
            fh.write(b'\0' * (self.NVS_BYTES - 1))
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', bad)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('Refusing', r.stderr)
        self.assertFalse(any('write-flash' in c for c in self.calls()), 'wrote a bad save')

    def assert_no_write(self, r, why):
        self.assertNotEqual(r.returncode, 0, why)
        self.assertFalse(any('write-flash' in c for c in self.calls()), why)

    def test_restore_refuses_a_save_that_fails_its_checksum(self):
        save = self.make_save()
        with open(save + '.sha256', 'w') as fh:
            fh.write('0' * 64 + '  pet.nvs.bin\n')
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assert_no_write(r, 'wrote a corrupted save')
        self.assertIn('.sha256', r.stderr)

    def test_restore_checks_the_file_not_the_name_written_in_the_sha256(self):
        # A renamed or copied pair still verifies: the hash is compared with
        # the file itself, whatever name the .sha256 records.
        save = self.make_save('original.nvs.bin')
        renamed = os.path.join(self.tmp, 'renamed.nvs.bin')
        os.rename(save, renamed)
        os.rename(save + '.sha256', renamed + '.sha256')
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', renamed)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_restore_refuses_a_save_with_no_sha256(self):
        save = self.make_save(sha=False)
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assert_no_write(r, 'wrote a save with nothing to check it against')
        self.assertIn('--force', r.stderr)
        self.assertEqual(self.calls(), [], 'must refuse before touching the board')

    def test_restore_force_allows_a_save_with_no_sha256(self):
        save = self.make_save(sha=False)
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save, '--force')
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(any('write-flash' in c for c in self.calls()))

    def test_restore_refuses_a_file_that_is_not_an_nvs_partition(self):
        save = self.make_save(first_word=b'\x00\x00\x00\x00')  # zeros: not a page state
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assert_no_write(r, 'wrote something that is not an NVS partition')
        self.assertIn('not an NVS partition', r.stderr)
        # --force waives only a missing checksum, not the format check
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save, '--force')
        self.assert_no_write(r, '--force must not skip the NVS format check')

    def test_restore_checks_every_page_header(self):
        save = self.make_save()
        with open(save, 'r+b') as fh:
            fh.seek(3 * 4096)
            fh.write(b'\x12\x34\x56\x78')
        import hashlib
        with open(save, 'rb') as fh, open(save + '.sha256', 'w') as sh:
            sh.write(hashlib.sha256(fh.read()).hexdigest() + '  pet.nvs.bin\n')
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assert_no_write(r, 'wrote a save with a bad page in the middle')

    def test_restore_accepts_a_page_esp_idf_marked_corrupt(self):
        save = self.make_save()
        with open(save, 'r+b') as fh:
            fh.seek(2 * 4096)
            fh.write(b'\xf0\xff\xff\xff')   # PageState::CORRUPT
        import hashlib
        with open(save, 'rb') as fh, open(save + '.sha256', 'w') as sh:
            sh.write(hashlib.sha256(fh.read()).hexdigest() + '  pet.nvs.bin\n')
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(any('write-flash' in c for c in self.calls()))

    def test_backup_warns_when_restore_would_refuse_the_file(self):
        dest = os.path.join(self.tmp, 'b', 'pet.nvs.bin')   # the fake reads zeros: not a page state
        r = self.run_tool('pet_save.sh', 'backup', '--port', self.port, '--file', dest)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('restore will refuse it', r.stderr)

    def test_restore_backs_up_the_current_pet_before_writing_nvs_only(self):
        save = self.make_save()
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assertEqual(r.returncode, 0, r.stderr)
        verbs = [next(a for a in c if a in ('read-flash', 'write-flash')) for c in self.calls()]
        self.assertEqual(verbs, ['read-flash', 'write-flash'], 'must back up before writing')
        read, write = self.calls()
        self.assertEqual([a for a in write if a.startswith('0x')], ['0x9000'])
        self.assertEqual(write[-2:], ['0x9000', save], 'must write the requested save, not the safety copy')
        self.assertEqual(read[read.index('--after') + 1], 'no-reset', 'the safety read must stay in the loader')
        self.assertEqual(write[write.index('--after') + 1], 'hard-reset', 'the board must restart after the write')
        backups = os.listdir(os.path.join(self.tmp, 'backups'))
        self.assertTrue(any(b.startswith('pre-restore-') and b.endswith('.nvs.bin') for b in backups))

    def test_restore_does_not_write_when_the_safety_backup_fails(self):
        save = self.make_save()
        self.env['FAKE_READ'] = 'fail'
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assert_no_write(r, 'wrote the board without a safety backup')
        self.assertTrue(any('read-flash' in c for c in self.calls()), 'the backup was never attempted')

    def test_restore_does_not_write_when_the_safety_backup_is_short(self):
        save = self.make_save()
        self.env['FAKE_READ'] = 'short'
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assert_no_write(r, 'wrote the board after a short safety backup')
        self.assertIn('not a usable backup', r.stderr)

    def test_pet_save_explains_that_the_action_comes_first(self):
        r = self.run_tool('pet_save.sh', '--port', self.port, 'backup')
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('action comes first', r.stderr)
        self.assertEqual(self.calls(), [])

    def test_help_stops_before_the_shell_options(self):
        for name in ('flash_175c.sh', 'update_175c.sh', 'pet_save.sh'):
            with self.subTest(script=name):
                r = self.run_tool(name, '--help')
                self.assertEqual(r.returncode, 0)
                self.assertNotIn('set -e', r.stdout)
                self.assertIn('Usage', r.stdout)

    def test_a_full_flash_needs_an_explicit_port(self):
        outdir = os.path.join(self.tmp, 'out')
        os.makedirs(outdir)
        put(os.path.join(outdir, 'TamaPoke.ino.bin'))
        r = self.run_tool('flash_175c.sh', '--out', outdir)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('explicit --port', r.stderr)
        self.assertEqual(self.calls(), [])

    def test_fw_only_also_needs_an_explicit_port(self):
        outdir = os.path.join(self.tmp, 'out')
        os.makedirs(outdir)
        put(os.path.join(outdir, 'TamaPoke.ino.bin'))
        r = self.run_tool('flash_175c.sh', '--out', outdir, '--fw-only')
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('explicit --port', r.stderr)
        self.assertEqual(self.calls(), [])

    def test_an_app_larger_than_app0_is_refused(self):
        outdir = os.path.join(self.tmp, 'out')
        os.makedirs(outdir)
        put(os.path.join(outdir, 'TamaPoke.ino.bin'), b'\0' * (0x300000 + 1))
        r = self.run_tool('flash_175c.sh', '--port', self.port, '--out', outdir, '--app-only')
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('larger than app0', r.stderr)
        self.assertEqual(self.calls(), [])

    def test_find_esptool_picks_the_newest_bundled_version(self):
        home = os.path.join(self.tmp, 'home')
        for v in ('4.9.0', '5.3.0', '5.10.0'):
            d = os.path.join(home, 'Library/Arduino15/packages/esp32/tools/esptool_py', v)
            os.makedirs(d)
            open(os.path.join(d, 'esptool'), 'w').close()
        env = {k: v for k, v in self.env.items() if k != 'ESPTOOL'}
        env['HOME'] = home
        out = subprocess.run(['bash', '-c', f'source "{TOOLS}/board_env.sh"; find_esptool'],
                             env=env, capture_output=True, text=True, check=True).stdout.strip()
        self.assertTrue(out.endswith('esptool_py/5.10.0/esptool'), out)

    def test_full_flash_falls_back_to_the_cores_boot_app0(self):
        home = os.path.join(self.tmp, 'home')
        core = os.path.join(home, 'Library/Arduino15/packages/esp32/hardware/esp32/3.3.10/tools')
        os.makedirs(os.path.join(core, 'partitions'))
        with open(os.path.join(core, 'gen_esp32part.py'), 'w') as fh:
            fh.write('import sys\nopen(sys.argv[-1], "wb").write(b"table")\n')
        boot_app0 = os.path.join(core, 'partitions', 'boot_app0.bin')
        put(boot_app0, b'otadata')
        outdir = os.path.join(self.tmp, 'out')   # a build output without boot_app0
        os.makedirs(outdir)
        for n in ('TamaPoke.ino.bin', 'TamaPoke.ino.bootloader.bin', 'storage.littlefs.bin'):
            put(os.path.join(outdir, n), b'x')
        self.env['HOME'] = home
        r = self.run_tool('flash_175c.sh', '--port', self.port, '--out', outdir)
        self.assertEqual(r.returncode, 0, r.stderr)
        (call,) = self.calls()
        self.assertIn(boot_app0, call, 'otadata must be written from the core when the build has none')
        self.assertEqual(call[call.index(boot_app0) - 1], '0xe000')

    def test_full_flash_uses_the_newest_core_whichever_folder_holds_it(self):
        # Both arrangements: sort order of the folder names differs between
        # BSD (macOS) and GNU sort, so one of them catches a whole-path sort.
        for newer_in in ('.arduino15', 'Library/Arduino15'):
            with self.subTest(newer_in=newer_in):
                home = tempfile.mkdtemp(dir=self.tmp)
                made = {}
                for base in ('.arduino15', 'Library/Arduino15'):
                    ver = '3.3.10' if base == newer_in else '3.3.9'
                    core = os.path.join(home, base, 'packages/esp32/hardware/esp32', ver, 'tools')
                    os.makedirs(os.path.join(core, 'partitions'))
                    with open(os.path.join(core, 'gen_esp32part.py'), 'w') as fh:
                        fh.write('import sys\nopen(sys.argv[-1], "wb").write(b"table")\n')
                    made[ver] = os.path.join(core, 'partitions', 'boot_app0.bin')
                    put(made[ver], ver.encode())
                outdir = os.path.join(home, 'out')
                os.makedirs(outdir)
                for n in ('TamaPoke.ino.bin', 'TamaPoke.ino.bootloader.bin', 'storage.littlefs.bin'):
                    put(os.path.join(outdir, n), b'x')
                if os.path.exists(self.log):
                    os.remove(self.log)
                self.env['HOME'] = home
                r = self.run_tool('flash_175c.sh', '--port', self.port, '--out', outdir)
                self.assertEqual(r.returncode, 0, r.stderr)
                (call,) = self.calls()
                self.assertIn(made['3.3.10'], call, 'picked the older core because of its folder name')

    def test_update_stops_before_flashing_when_the_build_fails(self):
        root = os.path.join(self.tmp, 'repo')
        shutil.copytree(TOOLS, os.path.join(root, 'tools'), ignore=shutil.ignore_patterns('build', '__pycache__'))
        shutil.copytree(os.path.join(ROOT, 'boards'), os.path.join(root, 'boards'))
        with open(os.path.join(root, 'tools', 'build_175c.sh'), 'w') as fh:
            fh.write('#!/usr/bin/env bash\nexit 1\n')
        outdir = os.path.join(self.tmp, 'out')   # an old build is there: it must not be flashed
        os.makedirs(outdir)
        put(os.path.join(outdir, 'TamaPoke.ino.bin'), b'old')
        r = subprocess.run([os.path.join(root, 'tools', 'update_175c.sh'), '--port', self.port, '--out', outdir],
                           env=self.env, capture_output=True, text=True, timeout=60)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('Stopped', r.stderr, r.stdout)
        self.assertFalse(any('write-flash' in c for c in self.calls()), 'flashed after a failed build')
        self.assertTrue(any('read-flash' in c for c in self.calls()), 'the pet should be backed up first')

    def test_update_builds_the_image_it_flashes_from_scratch(self):
        # The board's image must not reuse objects from an earlier (incremental) build.
        root = os.path.join(self.tmp, 'repo')
        shutil.copytree(TOOLS, os.path.join(root, 'tools'), ignore=shutil.ignore_patterns('build', '__pycache__'))
        shutil.copytree(os.path.join(ROOT, 'boards'), os.path.join(root, 'boards'))
        args = os.path.join(self.tmp, 'build-args')
        with open(os.path.join(root, 'tools', 'build_175c.sh'), 'w') as fh:
            fh.write(f'#!/usr/bin/env bash\nprintf "%s\\n" "$@" > "{args}"\nexit 1\n')
        r = subprocess.run([os.path.join(root, 'tools', 'update_175c.sh'), '--port', self.port,
                            '--out', os.path.join(self.tmp, 'out')],
                           env=self.env, capture_output=True, text=True, timeout=60)
        self.assertNotEqual(r.returncode, 0)
        with open(args) as fh:
            self.assertIn('--clean', fh.read().split('\n'), 'update_175c.sh builds incrementally')
        self.assertFalse(any('write-flash' in c for c in self.calls()))

    def test_update_says_why_the_clock_was_not_set(self):
        # A refused TIME (#45) must fail the update with the board's reason, not "did not
        # answer", and must not be retried: the refusal would come again. board.py is a
        # stub that logs each clock call, and `sleep` a no-op so a retry loop is quick.
        root = os.path.join(self.tmp, 'repo')
        shutil.copytree(TOOLS, os.path.join(root, 'tools'), ignore=shutil.ignore_patterns('build', '__pycache__'))
        shutil.copytree(os.path.join(ROOT, 'boards'), os.path.join(root, 'boards'))
        reason = "clock not set; the board said: ERR clock not set: it did not take 5; " \
                 "the pet's saved time is unchanged"
        clock_log = os.path.join(self.tmp, 'clock_calls')
        with open(os.path.join(root, 'tools', 'board.py'), 'w') as fh:
            fh.write('import sys\n'
                     'if "clock" in sys.argv:\n'
                     f'    open({clock_log!r}, "a").write("clock\\n")\n'
                     f'    sys.exit({reason!r})\n'
                     'sys.exit(1)\n')
        bindir = os.path.join(self.tmp, 'bin')
        os.makedirs(bindir)
        with open(os.path.join(bindir, 'sleep'), 'w') as fh:
            fh.write('#!/bin/sh\nexit 0\n')
        os.chmod(os.path.join(bindir, 'sleep'), 0o755)
        outdir = os.path.join(self.tmp, 'out')
        os.makedirs(outdir)
        put(os.path.join(outdir, 'TamaPoke.ino.bin'))
        env = dict(self.env, PATH=bindir + os.pathsep + self.env['PATH'])
        r = subprocess.run([os.path.join(root, 'tools', 'update_175c.sh'), '--port', self.port,
                            '--out', outdir, '--no-build'],
                           env=env, capture_output=True, text=True, timeout=60)
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertTrue(any('write-flash' in c for c in self.calls()), r.stderr)
        self.assertIn(reason, r.stderr)
        self.assertNotIn('did not answer on', r.stderr)
        self.assertNotIn('Update done', r.stdout)
        with open(clock_log) as fh:
            self.assertEqual(fh.read().split(), ['clock'], 'a refused TIME is sent once, not retried')

    def test_backup_reads_the_nvs_partition_with_the_rom_loader(self):
        dest = os.path.join(self.tmp, 'b', 'pet.nvs.bin')
        r = self.run_tool('pet_save.sh', 'backup', '--port', self.port, '--file', dest)
        self.assertEqual(r.returncode, 0, r.stderr)
        (call,) = self.calls()
        self.assertIn('--no-stub', call)
        self.assertEqual(call[call.index('read-flash') + 1:call.index('read-flash') + 3],
                         ['0x9000', '0x5000'])
        self.assertEqual(os.path.getsize(dest), self.NVS_BYTES)
        self.assertTrue(os.path.exists(dest + '.sha256'))


# Records each call's argv as one JSON line and fakes a good build: a small
# TamaPoke.ino.bin in --output-dir and, for the 1.75C (PartitionScheme=custom)
# when FAKE_PARTS is set, that partition table in --build-path (what
# build_175c.sh's prebuild hook copies; the original 1.75's scheme is built in
# and the fake leaves no table).
# FAKE_BIN_SIZE sets the image size, FAKE_NO_BIN writes none, FAKE_SLEEP waits
# that many seconds, FAKE_FAIL exits 1, FAKE_ORDER_LOG gets a "compile" line
# per call (to check the order of the gate's steps), and FAKE_WARNINGS=N prints
# N different warnings in the sketch (the last argument), one of them twice,
# plus one in a library. Like the real one, it refuses a sketch (the last
# argument) whose folder is not named after its main .ino.
FAKE_ARDUINO_CLI = r'''#!/usr/bin/env python3
import json, os, shutil, sys, time
args = sys.argv[1:]
with open(os.environ['FAKE_CLI_LOG'], 'a') as fh:
    fh.write(json.dumps(args) + '\n')
if os.environ.get('FAKE_ORDER_LOG'):
    with open(os.environ['FAKE_ORDER_LOG'], 'a') as fh:
        fh.write('compile\n')
main = os.path.join(args[-1], os.path.basename(os.path.normpath(args[-1])) + '.ino')
if not os.path.isfile(main):
    sys.exit(f"Error opening sketch: main file missing from sketch: {main}")
def opt(name):
    return args[args.index(name) + 1] if name in args else None
for i in range(int(os.environ.get('FAKE_WARNINGS', '0'))):
    for _ in range(2 if i == 0 else 1):
        print(f'{args[-1]}/pet.cpp:{i + 1}:5: warning: fake warning {i} [-Wfake]', file=sys.stderr)
if os.environ.get('FAKE_WARNINGS'):
    print('/home/x/Arduino/libraries/GFX_Library_for_Arduino/src/Arduino_GFX.cpp:1:1: warning: a library one [-Wfake]', file=sys.stderr)
if opt('--build-path'):
    os.makedirs(opt('--build-path'), exist_ok=True)
time.sleep(float(os.environ.get('FAKE_SLEEP', '0')))
if os.environ.get('FAKE_FAIL'):
    sys.exit(1)
if opt('--output-dir') and not os.environ.get('FAKE_NO_BIN'):
    os.makedirs(opt('--output-dir'), exist_ok=True)
    with open(os.path.join(opt('--output-dir'), 'TamaPoke.ino.bin'), 'wb') as fh:
        fh.write(b'\0' * int(os.environ.get('FAKE_BIN_SIZE', '1024')))
if opt('--build-path') and os.environ.get('FAKE_PARTS') and 'PartitionScheme=custom' in (opt('--fqbn') or ''):
    shutil.copy(os.environ['FAKE_PARTS'], os.path.join(opt('--build-path'), 'partitions.csv'))
'''


class FakeArduinoCliCase(unittest.TestCase):
    """A fake arduino-cli on PATH, so nothing here really compiles, and a HOME
    of the test's own, so the persistent build folders (tools/build_cache.sh)
    land in its temp dir and never in the owner's cache."""

    PARTS = os.path.join(ROOT, 'boards', '175c', 'partitions.csv')

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.log = os.path.join(self.tmp, 'arduino-cli.log')
        self.bindir = os.path.join(self.tmp, 'bin')
        os.makedirs(self.bindir)
        fake = os.path.join(self.bindir, 'arduino-cli')
        with open(fake, 'w') as fh:
            fh.write(FAKE_ARDUINO_CLI)
        os.chmod(fake, 0o755)
        self.home = os.path.join(self.tmp, 'home')
        os.makedirs(self.home)
        # An owner's own TAMAPOKE_JOBS (or board overrides, cache location or
        # warning baseline) must not leak in.
        self.env = {k: v for k, v in os.environ.items()
                    if k not in ('TAMAPOKE_JOBS', 'TAMAPOKE_OUT', 'FQBN_175C', 'APP_MAX_OVERRIDE',
                                 'XDG_CACHE_HOME', 'WARN_BASELINE')}
        # A stand-in sdl2-config first on PATH: the gate runs the interactive window's
        # selftest only where SDL2 works. FAKE_SDL=1 makes it work; by default it does
        # not, whatever the machine has installed.
        sdl = os.path.join(self.bindir, 'sdl2-config')
        with open(sdl, 'w') as fh:
            fh.write('#!/usr/bin/env bash\n[[ "${FAKE_SDL:-0}" == 1 ]] && { echo 2.0.0; exit 0; }\nexit 1\n')
        os.chmod(sdl, 0o755)
        self.env.update(PATH=self.bindir + os.pathsep + os.environ.get('PATH', ''), HOME=self.home,
                        FAKE_CLI_LOG=self.log, FAKE_PARTS=self.PARTS)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def fresh(self):
        """Forget earlier subtests' calls and the gate's stand-in steps."""
        for path in (self.log, getattr(self, 'ran', None)):
            if path and os.path.exists(path):
                os.remove(path)

    def calls(self):
        import json
        if not os.path.exists(self.log):
            return []
        with open(self.log) as fh:
            return [json.loads(line) for line in fh if line.strip()]

    def jobs_of(self, call):
        self.assertEqual(call.count('--jobs'), 1, call)
        self.assertNotIn('-j', call)
        return call[call.index('--jobs') + 1]

    def build_175c(self, *args, **env):
        return subprocess.run([os.path.join(TOOLS, 'build_175c.sh'), '--out', os.path.join(self.tmp, 'out'), *args],
                              env=dict(self.env, **env), capture_output=True, text=True, timeout=60)

    def gate_root(self, name='repo'):
        """A minimal repo for gate.sh: the real gate, 1.75C build script,
        build-cache helper and warning counter, the 1.75C table, a one-line sketch, and stand-ins
        for the host tests, the i18n check and the desktop simulator that
        record that they ran."""
        root = os.path.join(self.tmp, name)
        os.makedirs(os.path.join(root, 'tools'))
        os.makedirs(os.path.join(root, 'test'))
        os.makedirs(os.path.join(root, 'boards', '175c'))
        os.makedirs(os.path.join(root, 'sim', 'scripts'))
        put(os.path.join(root, 'sim', 'scripts', 'screens.txt'))   # no `shot` lines: 0/0 screens
        for script in ('gate.sh', 'build_175c.sh', 'build_cache.sh', 'sketch_warnings.sh'):
            shutil.copy2(os.path.join(TOOLS, script), os.path.join(root, 'tools', script))
        shutil.copy2(self.PARTS, os.path.join(root, 'boards', '175c', 'partitions.csv'))
        put(os.path.join(root, 'TamaPoke.ino'), b'void setup() {}\nvoid loop() {}\n')
        self.ran = os.path.join(self.tmp, 'ran')
        # FAKE_TEST_LOG: a file whose text the stand-in suite prints (compiler warnings, say);
        # FAKE_TEST_RC: its exit code (0 by default).
        stub = os.path.join(root, 'test', 'run_tests.sh')
        put(stub, (f'#!/usr/bin/env bash\necho tests >> "{self.ran}"\n'
                   f'[[ -n "${{FAKE_TEST_LOG:-}}" ]] && cat "$FAKE_TEST_LOG"\n'
                   f'echo "TODO OK"\nexit "${{FAKE_TEST_RC:-0}}"\n').encode())
        os.chmod(stub, 0o755)
        put(os.path.join(root, 'tools', 'test_i18n_formats.py'),
            f'open({self.ran!r}, "a").write("i18n\\n")\n'.encode())
        # --window is the interactive window's --selftest. FAKE_WINDOW picks what the
        # stand-in does: ok (the default), fail (exit 1, says FAILED), exit1 (exit 1 after
        # printing the ok line) or silent (exit 0 but without the "selftest ok" line).
        # A --script other than screens.txt is a script with checks (#99): the stand-in
        # records "check-<board>-<name>" in `ran` and its arguments in `args`, and reports
        # as many passed checks as the script has expect lines. FAKE_CHECKS picks otherwise,
        # for the script FAKE_CHECKS_SCRIPT names (all of them if unset): fail (one check
        # fails, exit 1), exit1 (all passed, exit 1), short (one fewer passed, exit 0),
        # silent (no count, exit 0) or extra (one more passed than the script has).
        self.args = os.path.join(self.tmp, 'args')
        sim = os.path.join(root, 'tools', 'sim.sh')
        put(sim, (f'#!/usr/bin/env bash\n'
                  f'if [[ " $* " == *" --window "* ]]; then\n'
                  f'  echo window >> "{self.ran}"\n'
                  f'  case "${{FAKE_WINDOW:-ok}}" in\n'
                  f'    ok) echo "window selftest ok: stub" ;;\n'
                  f'    fail) echo "window selftest FAILED: stub"; exit 1 ;;\n'
                  f'    exit1) echo "window selftest ok: stub"; exit 1 ;;\n'
                  f'    silent) ;;\n'
                  f'  esac\n'
                  f'  exit 0\n'
                  f'fi\n'
                  f'script=""; board=175c; prev=""\n'
                  f'for a in "$@"; do\n'
                  f'  [[ "$prev" == --script ]] && script="$a"\n'
                  f'  [[ "$prev" == --board ]] && board="$a"\n'
                  f'  prev="$a"\n'
                  f'done\n'
                  f'name="$(basename "$script" .txt)"\n'
                  f'if [[ "$name" == screens ]]; then echo sim >> "{self.ran}"; exit 0; fi\n'
                  f'echo "check-$board-$name" >> "{self.ran}"\n'
                  f'echo "$*" >> "{self.args}"\n'
                  f"n=$(grep -cE '^[[:space:]]*expect(-not|-once)?([[:space:]]|$)' \"$script\")\n"
                  f'mode=ok\n'
                  f'[[ -z "${{FAKE_CHECKS_SCRIPT:-}}" || "$FAKE_CHECKS_SCRIPT" == "$name" ]] && mode="${{FAKE_CHECKS:-ok}}"\n'
                  f'case "$mode" in\n'
                  f'  ok) echo "sim: checks $n/$n passed" ;;\n'
                  f'  fail) echo "$script:2: expect failed: stub" >&2; echo "sim: checks $((n - 1))/$n passed"; exit 1 ;;\n'
                  f'  exit1) echo "sim: checks $n/$n passed"; exit 1 ;;\n'
                  f'  short) echo "sim: checks $((n - 1))/$n passed" ;;\n'
                  f'  silent) ;;\n'
                  f'  extra) echo "sim: checks $((n + 1))/$((n + 1)) passed" ;;\n'
                  f'esac\n').encode())
        os.chmod(sim, 0o755)
        return root

    def gate(self, root, *args, **env):
        return subprocess.run([os.path.join(root, 'tools', 'gate.sh'), *args],
                              env=dict(self.env, **env), capture_output=True, text=True, timeout=120)


class TestBuildJobs(FakeArduinoCliCase):
    """build_175c.sh and the gate's original-1.75 build cap arduino-cli's
    parallel compile jobs (its own default is every core)."""

    # Both scripts carry their own copy of the rule; these pin both, from either
    # side. '08' is octal to arduino-cli (and invalid), '010' would be 8 jobs.
    GOOD_JOBS = ('1', '0', '8', '9999')
    BAD_JOBS = ('x', '08', '010', '-1', '3 ', '2.5', '10000')

    def test_build_175c_caps_compile_jobs_at_3_by_default(self):
        for env in ({}, {'TAMAPOKE_JOBS': ''}):
            with self.subTest(env=env):
                self.fresh()
                r = self.build_175c(**env)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertIn('1.75C build OK', r.stdout)
                (call,) = self.calls()
                self.assertEqual(call[0], 'compile')
                self.assertEqual(self.jobs_of(call), '3')

    def test_build_175c_takes_the_job_count_from_TAMAPOKE_JOBS(self):
        for jobs in self.GOOD_JOBS:
            with self.subTest(jobs=jobs):
                self.fresh()
                r = self.build_175c(TAMAPOKE_JOBS=jobs)
                self.assertEqual(r.returncode, 0, r.stderr)
                (call,) = self.calls()
                self.assertEqual(self.jobs_of(call), jobs)

    def test_build_175c_refuses_a_job_count_arduino_cli_would_misread(self):
        for bad in self.BAD_JOBS:
            with self.subTest(bad=bad):
                self.fresh()
                r = self.build_175c(TAMAPOKE_JOBS=bad)
                self.assertNotEqual(r.returncode, 0)
                self.assertIn('TAMAPOKE_JOBS', r.stderr)
                self.assertEqual(self.calls(), [], 'arduino-cli ran with a bad job count')

    def test_gate_caps_both_firmware_builds(self):
        root = self.gate_root()
        for env, jobs in (({}, '3'), *(({'TAMAPOKE_JOBS': j}, j) for j in self.GOOD_JOBS)):
            with self.subTest(env=env):
                self.fresh()
                r = self.gate(root, **env)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertIn('GATE GREEN', r.stdout)
                calls = self.calls()
                fqbns = sorted(c[c.index('--fqbn') + 1].split('FlashSize=')[1].split(',')[0] for c in calls)
                self.assertEqual(fqbns, ['16M', '32M'], 'expected the 1.75 and the 1.75C build')
                for call in calls:
                    self.assertEqual(self.jobs_of(call), jobs, call)

    def test_gate_refuses_a_bad_job_count_before_running_anything(self):
        root = self.gate_root()
        for bad in self.BAD_JOBS:
            with self.subTest(bad=bad):
                self.fresh()
                r = self.gate(root, TAMAPOKE_JOBS=bad)
                self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
                self.assertIn('TAMAPOKE_JOBS', r.stderr)
                self.assertFalse(os.path.exists(self.ran), 'the gate ran steps before checking TAMAPOKE_JOBS')
                self.assertEqual(self.calls(), [])
        # No builds, no compile jobs: --no-build doesn't care about the value.
        self.fresh()
        r = self.gate(root, '--no-build', TAMAPOKE_JOBS='08')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.calls(), [])

    def test_build_help_documents_TAMAPOKE_JOBS_and_stops_before_the_shell_options(self):
        for script in ('build_175c.sh', 'gate.sh'):
            with self.subTest(script=script):
                r = subprocess.run([os.path.join(TOOLS, script), '--help'], env=self.env,
                                   capture_output=True, text=True, timeout=30)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertIn('TAMAPOKE_JOBS', r.stdout)
                self.assertNotIn('set -', r.stdout)
                self.assertEqual(self.calls(), [])


class TestSketchWarnings(unittest.TestCase):
    """tools/sketch_warnings.sh: the sketch-warning count and WARN_BASELINE,
    shared by tools/gate.sh and CI's firmware builds."""

    SCRIPT = os.path.join(TOOLS, 'sketch_warnings.sh')
    ESC = '\033'

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.env = {k: v for k, v in os.environ.items() if k != 'WARN_BASELINE'}

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def log(self, sketch_warnings):
        """A build log with that many different sketch warnings, plus what must
        not count: a repeat, the same line in colour, a core and a library
        warning, an error, a note, and a warning in arduino-cli's generated
        TamaPoke.ino.cpp."""
        stage = '/cache/repo-0123/gate/stage/TamaPoke'
        lines = [f'{stage}/pet.cpp:{i + 1}:5: warning: unused variable [-Wunused-variable]'
                 for i in range(sketch_warnings)]
        if sketch_warnings:
            lines.append(lines[0])                                   # a header included twice
            lines.append(f'{self.ESC}[1m{lines[-1]}{self.ESC}[0m')   # the same, coloured
        lines += [
            '/home/x/.arduino15/packages/esp32/tools/cdc_host.h:162:58: warning: missing initializer',
            '/home/x/Arduino/libraries/GFX_Library_for_Arduino/src/Arduino_GFX.cpp:9:1: warning: unused [-Wunused]',
            f'{stage}/pet.cpp:3:1: error: expected ;',
            f'{stage}/pet.cpp:4:1: note: declared here',
            '/cache/repo-0123/gate/build-175/sketch/TamaPoke.ino.cpp:3:1: warning: generated',
            'Sketch uses 909806 bytes (28%) of program storage space. Maximum is 3145728 bytes.',
        ]
        path = os.path.join(self.tmp, 'build.log')
        put(path, ('\n'.join(lines) + '\n').encode())
        return path

    def run_script(self, *args, **env):
        return subprocess.run([self.SCRIPT, *args], env=dict(self.env, **env),
                              capture_output=True, text=True, timeout=30)

    def test_counts_each_sketch_warning_once_and_nothing_else(self):
        for n in (0, 1, 7):
            with self.subTest(n=n):
                r = self.run_script(self.log(n))
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual(r.stdout.strip(), f'{n} sketch warning(s), baseline 17')

    def test_more_than_the_baseline_fails(self):
        for n, baseline, rc in ((17, None, 0), (18, None, 1), (21, '21', 0), (22, '21', 1), (1, '0', 1)):
            with self.subTest(n=n, baseline=baseline):
                env = {'WARN_BASELINE': baseline} if baseline is not None else {}
                r = self.run_script(self.log(n), **env)
                self.assertEqual(r.returncode, rc, r.stdout + r.stderr)
                self.assertEqual(r.stdout.strip(), f'{n} sketch warning(s), baseline {baseline or 17}')
                if rc:
                    self.assertIn(f'+{n - int(baseline or 17)} NEW', r.stderr)

    def test_bad_input_is_an_error_not_a_pass(self):
        for args, env in (((os.path.join(self.tmp, 'missing.log'),), {}),
                          ((self.log(1),), {'WARN_BASELINE': 'x'}),
                          ((self.log(1),), {'WARN_BASELINE': '-1'}),
                          # leading zeros are octal to bash (( )); 08 is invalid, 010 would be 8
                          ((self.log(1),), {'WARN_BASELINE': '08'}),
                          ((self.log(1),), {'WARN_BASELINE': '010'}),
                          # too long: bash arithmetic overflows
                          ((self.log(1),), {'WARN_BASELINE': '12345'}),
                          ((self.log(1),), {'WARN_BASELINE': '99999999999999999999'}),
                          ((), {}),
                          ((self.log(1), 'extra'), {})):
            with self.subTest(args=args, env=env):
                r = self.run_script(*args, **env)
                self.assertEqual(r.returncode, 2, r.stdout + r.stderr)

    def test_ci_counts_both_boards_with_it(self):
        """CI's firmware builds keep their output and run this script on it, in
        a step that always runs after a good compile."""
        with open(os.path.join(ROOT, '.github', 'workflows', 'ci.yml')) as fh:
            ci = fh.read()
        job = re.search(r'^  firmware-build:\n(.*?)(?=^  \S)', ci, re.S | re.M).group(1)
        steps = re.split(r'^      - ', job, flags=re.M)[1:]
        compiles = [st for st in steps if st.startswith('name: Compile (')]
        self.assertEqual(len(compiles), 2, 'expected the 1.75 and the 1.75C compile steps')
        for st in compiles:
            with self.subTest(step=st.splitlines()[0]):
                self.assertRegex(st, r'\n        shell: bash\n', 'a tee without pipefail hides a failed compile')
                self.assertRegex(st, re.compile(r'2>&1 \| tee "\$RUNNER_TEMP/build\.log"$', re.M))
        after = steps[steps.index(compiles[-1]) + 1:]
        counts = [st for st in after if 'tools/sketch_warnings.sh "$RUNNER_TEMP/build.log"' in st]
        self.assertEqual(len(counts), 1, 'no warning count after the compiles')
        self.assertNotIn('if:', counts[0])
        self.assertNotIn('continue-on-error', counts[0])

    def test_ci_host_tests_fail_on_a_warning(self):
        """CI's host-test job keeps the suite's output and fails on any g++
        warning in it (#98). Without "shell: bash" (pipefail) the tee hides a
        failing suite, so the steps are pinned, and the check's own script is
        run the way Actions runs it."""
        with open(os.path.join(ROOT, '.github', 'workflows', 'ci.yml')) as fh:
            ci = fh.read()
        job = re.search(r'^  tests:\n(.*?)(?=^  \S|\Z)', ci, re.S | re.M).group(1)
        steps = re.split(r'^      - ', job, flags=re.M)[1:]
        suites = [st for st in steps if './test/run_tests.sh --asan' in st]
        self.assertEqual(len(suites), 1, 'expected one step that runs the suite')
        self.assertRegex(suites[0], r'\n        shell: bash\n', 'a tee without pipefail hides a failed suite')
        self.assertRegex(suites[0], re.compile(r'2>&1 \| tee "\$RUNNER_TEMP/tests\.log"$', re.M))
        # the check below looks only for warnings: a failing test fails the job through this step
        self.assertNotIn('continue-on-error', suites[0])
        self.assertNotRegex(suites[0], r'\n        if:')
        checks =[st for st in steps if st.startswith('name: No host build warnings')]
        self.assertEqual(len(checks), 1, 'expected the warning check step')
        check = checks[0]
        # the gate's host tests count with the same function (TestGateHostWarnings)
        self.assertIn('w="$(tp_host_warnings "$log")"', check)
        self.assertIn('$RUNNER_TEMP/tests.log', check)
        self.assertRegex(check, r'\n        shell: bash\n')
        self.assertRegex(check, r'\n        if: success\(\) \|\| failure\(\)\n', 'the check must run after a failed suite too')
        self.assertNotIn('continue-on-error', check)

        body = re.search(r'\n        run: \|\n(.*)', check, re.S).group(1)
        body = '\n'.join(ln[10:] for ln in body.splitlines())
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp, True)
        log = os.path.join(tmp, 'tests.log')
        line = '../a.cpp:1:2: warning: x [-Wfoo]\n'
        coloured = '../a.cpp:1:2: \033[01;35m\033[Kwarning: \033[m\033[Kx\n'

        def run(text):
            if os.path.exists(log):
                os.remove(log)
            if text is not None:
                with open(log, 'w') as fh:
                    fh.write(text)
            # the flags Actions gives "shell: bash"
            return subprocess.run(['bash', '--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', body],
                                  cwd=ROOT, env=dict(os.environ, RUNNER_TEMP=tmp),
                                  capture_output=True, text=True)

        for name, text, rc, out in (
                ('clean', 'ok\n1 warning-free line\n', 0, '0 host build warnings'),
                # the normal and the ASan build print the same line
                ('twice', 'ok\n' + line + line, 1, '1 host build warning(s)'),
                ('coloured', coloured, 1, '1 host build warning(s)'),
                ('missing', None, 1, 'no suite output')):
            with self.subTest(log=name):
                r = run(text)
                self.assertEqual(r.returncode, rc, r.stdout + r.stderr)
                self.assertIn(out, r.stdout + r.stderr)

    def test_the_gate_counts_with_it(self):
        with open(os.path.join(TOOLS, 'gate.sh')) as fh:
            gate = fh.read()
        self.assertIn('. "$ROOT/tools/sketch_warnings.sh"', gate)
        self.assertNotIn('WARN_BASELINE=', gate, 'the gate has its own baseline again')
        # host build warnings: the function CI's "No host build warnings" step counts with
        self.assertIn('hw="$(tp_host_warnings "$TMP/tests.log" | wc -l', gate)
        self.assertNotIn("grep -E ': warning: '", gate, 'the gate counts host warnings its own way again')


class TestBuildCache(FakeArduinoCliCase):
    """Incremental firmware builds (tools/build_cache.sh): one persistent build
    folder per checkout and board, the sketch compiled again on every run, the
    gate's stable staged copy, the lock that keeps two runs out of one folder,
    --clean, and pruning the folders of checkouts that are gone."""

    APP0 = 0x300000   # boards/175c/partitions.csv

    def setUp(self):
        super().setUp()
        self.uname('Linux')   # the cache lives in ~/.cache unless a test says otherwise

    def uname(self, system):
        path = os.path.join(self.bindir, 'uname')
        put(path, f'#!/bin/sh\necho {system}\n'.encode())
        os.chmod(path, 0o755)

    @staticmethod
    def opt(call, name):
        return call[call.index(name) + 1]

    @staticmethod
    def owner_of(pid):
        """What tools/build_cache.sh records for a running process (its start
        time in UTC and the C locale)."""
        start = subprocess.run(['ps', '-o', 'lstart=', '-p', str(pid)], capture_output=True, text=True,
                               env=dict(os.environ, LC_ALL='C', TZ='UTC0')).stdout.strip()
        return f'{pid} {start}'

    @staticmethod
    def hold(folder, owner, age=0):
        """A lock on <folder> as another run would leave it; owner None means
        it has no owner recorded, age backdates it by that many seconds."""
        shutil.rmtree(folder + '.lock', ignore_errors=True)
        os.makedirs(folder + '.lock')
        if owner is not None:
            put(os.path.join(folder + '.lock', 'owner'), (owner + '\n').encode())
        if age:
            then = time.time() - age
            os.utime(folder + '.lock', (then, then))

    def build_path(self, **env):
        """The 1.75C build folder build_175c.sh uses for this checkout."""
        self.fresh()
        r = self.build_175c(**env)
        self.assertEqual(r.returncode, 0, r.stderr)
        (call,) = self.calls()
        self.fresh()
        return self.opt(call, '--build-path')

    def gate_calls(self, root, *args, **env):
        """Run the gate and return its two compile calls, 1.75C first."""
        self.fresh()
        r = self.gate(root, *args, **env)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        calls = self.calls()
        self.assertEqual(len(calls), 2, calls)
        return sorted(calls, key=lambda c: '16M' in self.opt(c, '--fqbn'))

    DAY = 24 * 3600

    def standalone(self, root, *args, **env):
        """Run <root>'s own build_175c.sh; return its run and its one compile call."""
        self.fresh()
        r = subprocess.run([os.path.join(root, 'tools', 'build_175c.sh'), '--out', os.path.join(self.tmp, 'out'), *args],
                           env=dict(self.env, **env), capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stderr)
        (call,) = self.calls()
        self.fresh()
        return r, call

    def checkout(self, name, git):
        """A checkout of its own that a standalone build gave a cache folder: a
        git worktree (git='worktree': its .git is a file), a clone ('clone': a
        .git folder) or neither (None). Returns its root and cache folder."""
        root = self.gate_root(name)
        if git == 'worktree':
            put(os.path.join(root, '.git'), b'gitdir: /elsewhere/.git/worktrees/x\n')
        elif git == 'clone':
            os.makedirs(os.path.join(root, '.git'))
        _, call = self.standalone(root)
        return root, os.path.dirname(self.opt(call, '--build-path'))

    def last_built(self, cache, days_ago):
        """Make the last firmware build that used <cache> <days_ago> days old;
        return that time."""
        then = time.time() - days_ago * self.DAY
        os.utime(os.path.join(cache, 'checkout'), (then, then))
        return then

    def build_while_compiling(self, during):
        """Run build_175c.sh with a slow compile; once arduino-cli is running (the
        prune is over), call during(); then end the build. Returns its stderr."""
        import signal
        self.fresh()
        p = subprocess.Popen([os.path.join(TOOLS, 'build_175c.sh'), '--out', os.path.join(self.tmp, 'out')],
                             env=dict(self.env, FAKE_SLEEP='30'), start_new_session=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.time() + 20
            while not self.calls() and time.time() < deadline:
                time.sleep(0.05)
            self.assertEqual(len(self.calls()), 1, 'the build never started compiling')
            during()
            # A build that already ended gave every lock back from its EXIT
            # trap, so during() would have seen "given back" for nothing.
            self.assertIsNone(p.poll(), 'the build had already exited: its EXIT trap may have given the locks back')
        finally:
            os.killpg(p.pid, signal.SIGTERM)
            _, err = p.communicate(timeout=20)
        for failure in ('Error opening sketch', 'Could not stage'):
            self.assertNotIn(failure, err)
        return err

    @staticmethod
    def files(folder):
        """Every file under <folder>, as sorted relative paths."""
        return sorted(os.path.relpath(os.path.join(d, f), folder) for d, _, fs in os.walk(folder) for f in fs)

    def test_each_checkout_keeps_one_build_folder_in_the_user_cache(self):
        xdg = os.path.join(self.tmp, 'xdg')
        for system, env, base in (
                ('Darwin', {}, os.path.join(self.home, 'Library', 'Caches', 'tamapoke')),
                ('Linux', {}, os.path.join(self.home, '.cache', 'tamapoke')),
                ('Linux', {'XDG_CACHE_HOME': xdg}, os.path.join(xdg, 'tamapoke'))):
            with self.subTest(system=system, env=env):
                self.uname(system)
                first = self.build_path(**env)
                put(os.path.join(first, 'kept'))   # say, a compiled library
                self.assertEqual(self.build_path(**env), first, 'a rebuild must use the same build folder')
                self.assertTrue(os.path.exists(os.path.join(first, 'kept')), 'the build folder was not kept')
                folder = os.path.dirname(first)
                self.assertEqual(os.path.dirname(folder), base)
                self.assertEqual(os.path.basename(first), 'build-175c')
                self.assertTrue(os.path.basename(folder).startswith(os.path.basename(ROOT) + '-'), folder)
        # Two checkouts never share one, even when their folders have the same name.
        self.uname('Linux')
        paths = set()
        for where in ('one', 'two'):
            root = self.gate_root(os.path.join(where, 'TamaPoke'))
            self.fresh()
            r = subprocess.run([os.path.join(root, 'tools', 'build_175c.sh'), '--out', os.path.join(self.tmp, 'out')],
                               env=self.env, capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, r.stderr)
            (call,) = self.calls()
            paths.add(self.opt(call, '--build-path'))
        self.assertEqual(len(paths), 2, paths)

    def test_a_rebuild_recompiles_the_sketch_and_reuses_the_rest(self):
        root = self.gate_root()
        put(os.path.join(root, 'pet.cpp'))   # the real sketch has one too
        builds = [self.build_path()] + [self.opt(c, '--build-path') for c in self.gate_calls(root)]

        def fill(build, *extra):
            """What arduino-cli leaves in a build folder: its copy of the sketch with
            the objects and library-detection files, the libraries and the core."""
            for part in ('sketch/pet.cpp', 'sketch/pet.cpp.o', 'sketch/pet.cpp.d', 'sketch/pet.cpp.libsdetect.d',
                         'sketch/TamaPoke.ino.cpp', 'sketch/TamaPoke.ino.cpp.merged', 'sketch/TamaPoke.ino.cpp.o',
                         'sketch/objs.a', 'libraries/GFX_Library_for_Arduino/Arduino_GFX.cpp.o', 'core/core.a', *extra):
                os.makedirs(os.path.dirname(os.path.join(build, part)), exist_ok=True)
                put(os.path.join(build, part))

        def rebuild():
            self.assertEqual(self.build_path(), builds[0])
            self.assertEqual([self.opt(c, '--build-path') for c in self.gate_calls(root)], builds[1:])

        for build in builds:
            fill(build)
        rebuild()
        for build in builds:
            with self.subTest(build=build):
                # a kept sketch object would hide its warnings from the gate's count
                for obj in ('pet.cpp.o', 'TamaPoke.ino.cpp.o'):
                    self.assertFalse(os.path.exists(os.path.join(build, 'sketch', obj)), f'{obj} was kept')
                self.assertTrue(os.path.exists(os.path.join(build, 'sketch', 'pet.cpp.libsdetect.d')),
                                'library detection was not reused')
                self.assertTrue(os.path.exists(os.path.join(build, 'libraries', 'GFX_Library_for_Arduino', 'Arduino_GFX.cpp.o')))
                self.assertTrue(os.path.exists(os.path.join(build, 'core', 'core.a')))
        # A copy of a file the sketch no longer has (deleted or renamed) would
        # still be compiled or included: arduino-cli's copy starts over.
        for build in builds:
            fill(build, 'sketch/renamed.cpp')
        rebuild()
        for build in builds:
            with self.subTest(build=build, stale='renamed.cpp'):
                self.assertFalse(os.path.exists(os.path.join(build, 'sketch')), 'a stale copy was kept')
                self.assertTrue(os.path.exists(os.path.join(build, 'libraries', 'GFX_Library_for_Arduino', 'Arduino_GFX.cpp.o')))

    def test_clean_builds_from_empty_folders(self):
        build = self.build_path()
        root = self.gate_root()
        gate_builds = [self.opt(c, '--build-path') for c in self.gate_calls(root)]
        stage = self.gate_calls(root)[1][-1]
        leftovers = [os.path.join(b, 'libraries', 'old.o') for b in [build] + gate_builds]
        leftovers.append(os.path.join(os.path.dirname(stage), 'old-stage-file'))
        # build_175c.sh's own staged copy, beside its build folder
        leftovers.append(os.path.join(os.path.dirname(build), 'stage', 'old-stage-file'))
        for path in leftovers:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            put(path)
        # without --clean everything is kept, and arduino-cli is not asked to clean
        self.assertEqual(self.build_path(), build)
        for call in self.gate_calls(root):
            self.assertNotIn('--clean', call)
        for path in leftovers:
            self.assertTrue(os.path.exists(path), path)
        # with it nothing is
        self.fresh()
        r = self.build_175c('--clean')
        self.assertEqual(r.returncode, 0, r.stderr)
        (call,) = self.calls()
        self.assertIn('--clean', call)
        for call in self.gate_calls(root, '--clean'):
            self.assertIn('--clean', call)
        for path in leftovers:
            self.assertFalse(os.path.exists(path), f'{path} survived --clean')
        # --clean is about the builds, which --no-build skips
        self.fresh()
        r = self.gate(root, '--clean', '--no-build')
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn('--clean', r.stderr)
        self.assertFalse(os.path.exists(self.ran))
        for script in ('build_175c.sh', 'gate.sh'):
            r = subprocess.run([os.path.join(TOOLS, script), '--help'], env=self.env,
                               capture_output=True, text=True, timeout=30)
            self.assertIn('--clean', r.stdout, script)

    def test_a_folder_in_use_by_another_run_is_refused(self):
        owner = self.owner_of(os.getpid())   # a running process: this test
        build = self.build_path()
        self.hold(build, owner)
        r = self.build_175c()
        self.assertNotEqual(r.returncode, 0, 'built in a folder another run holds')
        self.assertIn('in use by another run', r.stderr)
        self.assertIn(str(os.getpid()), r.stderr)
        self.assertEqual(self.calls(), [], 'built in a folder another run holds')
        with open(os.path.join(build + '.lock', 'owner')) as fh:
            self.assertEqual(fh.read().strip(), owner, "another run's lock was touched")
        # a lock just taken, its owner not written yet
        self.hold(build, None)
        r = self.build_175c()
        self.assertNotEqual(r.returncode, 0, 'built in a folder a run is just taking')
        self.assertIn('not recorded yet', r.stderr)
        self.assertEqual(self.calls(), [])
        # one gate at a time per checkout: refused before any step runs
        root = self.gate_root()
        gate_dir = os.path.dirname(self.opt(self.gate_calls(root)[0], '--build-path'))
        self.hold(gate_dir, owner)
        self.fresh()
        r = self.gate(root)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn('in use by another run', r.stderr)
        self.assertFalse(os.path.exists(self.ran), 'the gate ran steps while another gate held the lock')
        self.assertEqual(self.calls(), [])
        self.assertTrue(os.path.isdir(gate_dir + '.lock'))
        # --no-build touches no build folder, so it needs no lock
        r = self.gate(root, '--no-build')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_a_standalone_build_takes_its_lock_before_it_stages(self):
        # On its own, build-175c.lock covers the staged copy as well: a second run
        # must be refused before it touches the stage the first one is compiling.
        root = self.gate_root('wt')
        _, call = self.standalone(root)
        cache = os.path.dirname(self.opt(call, '--build-path'))
        stage = call[-1]
        staged = self.files(stage)
        self.hold(os.path.join(cache, 'build-175c'), self.owner_of(os.getpid()))
        put(os.path.join(root, 'new.cpp'))
        r = subprocess.run([os.path.join(root, 'tools', 'build_175c.sh'), '--out', os.path.join(self.tmp, 'out')],
                           env=self.env, capture_output=True, text=True, timeout=60)
        self.assertNotEqual(r.returncode, 0, 'built in a folder another run holds')
        self.assertIn('in use by another run', r.stderr)
        self.assertEqual(self.calls(), [])
        self.assertEqual(self.files(stage), staged, "the stage a running build reads was rewritten")

    def test_a_lock_whose_owner_is_gone_is_taken_over(self):
        gone = subprocess.Popen(['true'])
        gone.wait()
        build = self.build_path()
        for owner, age in ((f'{gone.pid} Thu Jan  1 00:00:00 1970', 0),       # no such process any more
                           (f'{os.getpid()} Thu Jan  1 00:00:00 1970', 0),   # its pid now belongs to another process
                           (None, 120)):        # its taker died before writing the owner, two minutes ago
            with self.subTest(owner=owner):
                self.hold(build, owner, age)
                self.fresh()
                r = self.build_175c()
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertIn('Removed a stale lock', r.stderr)
                self.assertEqual(len(self.calls()), 1)
                self.assertFalse(os.path.exists(build + '.lock'))

    def test_the_lock_holds_whatever_the_locale_or_time_zone(self):
        # ps prints a start time in the caller's locale and time zone: a run from
        # Terminal (say en_GB) and one from an agent's shell (C) must still see
        # the same owner. Two real runs; the second must be refused.
        import signal
        build = self.build_path()
        holder = subprocess.Popen([os.path.join(TOOLS, 'build_175c.sh'), '--out', os.path.join(self.tmp, 'out')],
                                  env=dict(self.env, FAKE_SLEEP='30', LC_ALL='en_GB.UTF-8', TZ='XYZ-9'),
                                  start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.time() + 20
            while not self.calls() and time.time() < deadline:
                time.sleep(0.05)
            self.assertEqual(len(self.calls()), 1, 'the first run never started compiling')
            r = self.build_175c(LC_ALL='C', TZ='UTC0')
            self.assertNotEqual(r.returncode, 0, 'took over the lock of a run that is still compiling')
            self.assertIn('in use by another run', r.stderr)
            self.assertEqual(len(self.calls()), 1, 'two runs compiled in one build folder')
        finally:
            os.killpg(holder.pid, signal.SIGTERM)
            holder.wait(timeout=20)
        self.assertFalse(os.path.exists(build + '.lock'))

    def test_the_lock_is_given_back_however_the_run_ends(self):
        import signal
        build = self.build_path()
        self.assertFalse(os.path.exists(build + '.lock'), 'kept after a good build')
        r = self.build_175c(FAKE_FAIL='1')
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse(os.path.exists(build + '.lock'), 'kept after a failed build')
        root = self.gate_root()
        gate_dir = os.path.dirname(self.opt(self.gate_calls(root)[0], '--build-path'))
        self.assertFalse(os.path.exists(gate_dir + '.lock'), 'kept after a green gate')
        r = self.gate(root, FAKE_FAIL='1')
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse(os.path.exists(gate_dir + '.lock'), 'kept after a failed gate')
        # stopped while compiling (Ctrl-C or a kill): the trap still gives it back
        self.fresh()
        p = subprocess.Popen([os.path.join(TOOLS, 'build_175c.sh'), '--out', os.path.join(self.tmp, 'out')],
                             env=dict(self.env, FAKE_SLEEP='30'), start_new_session=True,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.time() + 20
            while not self.calls() and time.time() < deadline:
                time.sleep(0.05)
            with open(os.path.join(build + '.lock', 'owner')) as fh:
                self.assertEqual(fh.read().split()[0], str(p.pid), 'the running build does not hold the lock')
            os.killpg(p.pid, signal.SIGTERM)
            p.wait(timeout=20)
        finally:
            if p.poll() is None:
                os.killpg(p.pid, signal.SIGKILL)
                p.wait()
        self.assertFalse(os.path.exists(build + '.lock'), 'kept after the build was stopped')

    def test_the_cache_of_a_removed_checkout_is_deleted(self):
        # A build folder takes about 1.5 GB: a removed worktree must not leave its own behind.
        roots, caches = {}, {}
        for name in ('kept', 'removed', 'busy', 'later'):
            roots[name] = self.gate_root(name)
            caches[name] = os.path.dirname(os.path.dirname(self.opt(self.gate_calls(roots[name])[0], '--build-path')))
        base = os.path.dirname(caches['kept'])
        stray = os.path.join(base, 'not-ours')   # no record of a checkout: never ours to delete
        os.makedirs(stray)
        self.hold(os.path.join(caches['busy'], 'gate'), self.owner_of(os.getpid()))   # a run still going
        for name in ('removed', 'busy'):
            shutil.rmtree(roots[name])
        self.build_path()   # a build in any checkout prunes: here build_175c.sh in this one
        self.assertFalse(os.path.exists(caches['removed']), "a removed checkout's cache was kept")
        self.assertTrue(os.path.exists(caches['kept']))
        self.assertTrue(os.path.exists(caches['busy']), 'deleted while a run held its lock')
        self.assertTrue(os.path.exists(stray), 'deleted a folder with no record of its checkout')
        # the gate prunes too
        shutil.rmtree(roots['later'])
        self.gate_calls(roots['kept'])
        self.assertFalse(os.path.exists(caches['later']), "the gate kept a removed checkout's cache")
        self.assertTrue(os.path.exists(caches['busy']))

    def test_an_idle_worktrees_cache_is_deleted_unless_a_run_holds_it(self):
        # A worktree's cache folder takes up to about 5 GB, and an agent's worktree
        # stays on disk long after its last build: a week without one, and it goes.
        me = self.owner_of(os.getpid())   # a run that is still going: this test
        dead = subprocess.Popen(['true'])
        dead.wait()
        roots, caches = {}, {}
        for name, git in (('idle', 'worktree'), ('recent', 'worktree'), ('held', 'worktree'),
                          ('held-inside', 'worktree'), ('dead-lock', 'worktree'), ('clone', 'clone'),
                          ('no-git', None), ('removed', 'worktree')):
            roots[name], caches[name] = self.checkout(name, git)
        built = {name: self.last_built(cache, 7.1) for name, cache in caches.items()}
        self.last_built(caches['recent'], 6.9)
        shutil.rmtree(roots['removed'])
        self.hold(os.path.join(caches['held'], 'gate'), me)                       # a gate running there
        self.hold(os.path.join(caches['held-inside'], 'gate', 'build-175c'), me)  # that gate's own 1.75C build
        self.hold(os.path.join(caches['dead-lock'], 'build-175c'), f'{dead.pid} Thu Jan  1 00:00:00 1970')
        put(os.path.join(caches['idle'], 'build-175c', 'big.o'), b'\1' * (3 << 20))
        # Finder's files: a folder with one is not deleted by a glob that skips hidden entries
        put(os.path.join(caches['idle'], '.DS_Store'))
        put(os.path.join(caches['idle'], '..odd'))
        before = {name: self.files(caches[name]) for name in ('held', 'held-inside')}

        def locks_given_back():
            # The prune is over and this build is still compiling: the locks it
            # claimed in a folder it then kept are back, or a build there would
            # be refused "in use" for as long as this one runs.
            self.assertFalse(os.path.exists(os.path.join(caches['held'], 'build-175c.lock')),
                             'held: the lock taken before the held one was not given back')
            with open(os.path.join(caches['held'], 'gate.lock', 'owner')) as fh:
                self.assertEqual(fh.read().strip(), me, "held: another run's lock was touched")
            for lock in ('build-175c.lock', 'gate.lock'):
                self.assertFalse(os.path.exists(os.path.join(caches['held-inside'], lock)),
                                 f'held-inside: {lock} was not given back')
        # a firmware build in any checkout prunes: here build_175c.sh in this one
        err = self.build_while_compiling(locks_given_back)
        self.assertEqual({name for name, cache in caches.items() if os.path.exists(cache)},
                         {'recent', 'held', 'held-inside', 'clone', 'no-git'},
                         'only git worktrees idle for 7 days, and removed checkouts, go; never a held one')
        for name, lock in (('held', 'gate.lock'), ('held-inside', os.path.join('gate', 'build-175c.lock'))):
            self.assertEqual(self.files(caches[name]), before[name], f'{name}: a held folder was touched')
            with open(os.path.join(caches[name], lock, 'owner')) as fh:
                self.assertEqual(fh.read().strip(), me, f"{name}: another run's lock was touched")
        # One line for each folder deleted: whose it was, why, and what it freed.
        said = [line for line in err.splitlines() if line.startswith('Build cache:')]
        self.assertEqual(len(said), 3, err)
        for name in ('idle', 'dead-lock'):
            when = time.strftime('%Y-%m-%d', time.localtime(built[name]))
            self.assertIn(f'Build cache: deleted the folder of {roots[name]} (no firmware build there since {when}): ',
                          err)
        self.assertIn(f'Build cache: deleted the folder of {roots["removed"]} (its checkout no longer exists): ',
                      err)
        self.assertRegex(err, rf'folder of {re.escape(roots["idle"])} \([^)]*\): 3 MB freed\.')

    def remove(self, folder):
        """tp_cache_remove on <folder>, called as a prune would (the real prune only
        calls it for a stale folder; here it is called regardless). Returns its run."""
        return subprocess.run(['bash', '-c', 'source "$1/build_cache.sh"; tp_cache_remove "$2"', '_', TOOLS, folder],
                              env=self.env, capture_output=True, text=True, timeout=30)

    def test_a_folder_that_is_not_stale_after_all_is_kept_once_its_locks_are_held(self):
        # A prune decides on a folder, then takes its locks: a build there may have
        # written its record meanwhile (or another prune deleted the folder).
        root, cache = self.checkout('wt', 'worktree')
        before = self.files(cache)
        r = self.remove(cache)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertEqual(self.files(cache), before, 'deleted a folder that is not stale')
        self.assertEqual(r.stderr, '')
        self.assertEqual(sorted(n for n in os.listdir(cache) if n.endswith('.lock')), [], 'left its locks behind')
        # a folder another prune deleted: taking the locks made it again, empty
        gone = os.path.join(os.path.dirname(cache), 'gone-0123456789ab')
        r = self.remove(gone)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertFalse(os.path.exists(gone), 'left an empty folder behind')

    def test_a_build_that_starts_while_its_folder_is_deleted_is_refused(self):
        # While a prune deletes a worktree's folder it holds both locks the builds
        # there take, by the names gate.sh and build_175c.sh really use: a gate or
        # a standalone build that starts there then is refused, instead of
        # building in a folder that is going away. du runs right before the
        # deletion; a stand-in for it starts both builds there and then.
        import json
        root, cache = self.checkout('wt', 'worktree')
        self.last_built(cache, 8)
        seen = os.path.join(self.tmp, 'seen.json')
        real_du = shutil.which('du')
        put(os.path.join(self.bindir, 'du'), f'''#!/usr/bin/env python3
import json, os, subprocess, sys
if os.environ.get('IN_FAKE_DU'):
    os.execv({real_du!r}, ['du'] + sys.argv[1:])
folder = sys.argv[-1]
def owners():
    out = {{}}
    for name in ('build-175c', 'gate'):
        try:
            with open(os.path.join(folder, name + '.lock', 'owner')) as fh:
                out[name] = fh.read().strip()
        except OSError:
            out[name] = None
    return out
got = {{'before': owners()}}
for script, args in (('build_175c.sh', ['--out', {os.path.join(self.tmp, 'out-wt')!r}]), ('gate.sh', [])):
    r = subprocess.run([os.path.join({root!r}, 'tools', script), *args], env=dict(os.environ, IN_FAKE_DU='1'),
                       capture_output=True, text=True, timeout=120)
    got[script] = [r.returncode, r.stderr]
got['after'] = owners()
with open({seen!r}, 'w') as fh:
    json.dump(got, fh)
os.execv({real_du!r}, ['du'] + sys.argv[1:])
'''.encode())
        os.chmod(os.path.join(self.bindir, 'du'), 0o755)
        r = self.build_175c()   # prunes the idle worktree's folder
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(seen) as fh:
            got = json.load(fh)
        owner = got['before']['build-175c']
        self.assertIsNotNone(owner, 'the prune deleted without holding build-175c.lock')
        self.assertEqual(got['before']['gate'], owner, 'the prune deleted without holding gate.lock')
        for script, rc in (('build_175c.sh', 1), ('gate.sh', 2)):
            code, err = got[script]
            self.assertEqual(code, rc, f'{script} started in a folder being deleted: {err}')
            self.assertIn('in use by another run', err)
        self.assertEqual(got['after'], got['before'], "a refused build touched the prune's locks")
        self.assertFalse(os.path.exists(cache))
        self.assertIn(f'Build cache: deleted the folder of {root} (no firmware build there since', r.stderr)

    def test_every_firmware_build_marks_its_cache_used(self):
        # The idle prune goes by the age of the checkout record, so every
        # firmware build must write it again, whether it changed or not.
        root, cache = self.checkout('wt', 'worktree')
        record = os.path.join(cache, 'checkout')
        for how in ('build_175c.sh', 'gate'):
            with self.subTest(how=how):
                self.last_built(cache, 30)
                if how == 'gate':
                    self.gate_calls(root)
                else:
                    self.standalone(root)
                self.assertLess(time.time() - os.stat(record).st_mtime, 60, f'{how} did not mark the cache used')
        # a gate that builds no firmware uses no build folder
        self.last_built(cache, 30)
        r = self.gate(root, '--no-firmware')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertGreater(time.time() - os.stat(record).st_mtime, 29 * self.DAY)

    @unittest.skipIf(os.geteuid() == 0, 'root deletes from a read-only folder anyway')
    def test_a_deletion_that_could_not_finish_is_finished_by_the_next_build(self):
        root, cache = self.checkout('wt', 'worktree')
        self.last_built(cache, 8)
        stuck = os.path.join(cache, 'build-175c', 'stuck')
        os.makedirs(stuck)
        put(os.path.join(stuck, 'file.o'))
        os.chmod(stuck, 0o500)   # its file cannot be deleted

        def given_back():
            # The prune is over and this build is still compiling: the worktree's
            # locks are back already, or a build there would be refused meanwhile.
            for lock in ('build-175c.lock', 'gate.lock'):
                self.assertFalse(os.path.exists(os.path.join(cache, lock)), f'{lock} was not given back')
            self.assertTrue(os.path.exists(os.path.join(cache, 'checkout')),
                            'the record went first: no later prune would find the rest')
        try:
            err = self.build_while_compiling(given_back)
            self.assertIn(f'Build cache: could not delete all of {cache} (no firmware build there since', err)
            self.assertIn(f"delete it by hand while no build runs: rm -rf '{cache}'", err)
        finally:
            os.chmod(stuck, 0o700)
        r = self.build_175c()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertFalse(os.path.exists(cache), 'the next build did not finish the deletion')
        self.assertIn(f'Build cache: deleted the folder of {root} (no firmware build there since', r.stderr)

    def test_a_standalone_build_compiles_the_gates_narrowed_stage(self):
        # Every agent worktree's folder is named otherwise than TamaPoke.ino,
        # which arduino-cli refuses; and a file added or removed anywhere under
        # the sketch folder made arduino-cli rebuild everything.
        root = self.gate_root('agent-worktree')
        for part in ('pet.cpp', 'test/test_x.cpp', 'sim/x.cpp', 'docs/x.md', 'web/x.json',
                     '.claude/worktrees/other/y.h', 'tools/other.py',
                     'build/esp32.esp32.esp32s3/x.bin'):   # arduino-cli's export folder, gitignored
            os.makedirs(os.path.dirname(os.path.join(root, part)), exist_ok=True)
            put(os.path.join(root, part))
        _, call = self.standalone(root)
        cache = os.path.dirname(self.opt(call, '--build-path'))
        stage = call[-1]
        self.assertEqual(stage, os.path.join(cache, 'stage', 'TamaPoke'))
        self.assertEqual(self.opt(call, '--build-path'), os.path.join(cache, 'build-175c'))
        staged = self.files(stage)
        self.assertEqual(staged, sorted(['TamaPoke.ino', 'pet.cpp', os.path.join('boards', '175c', 'partitions.csv'),
                                         os.path.join('tools', 'build_175c.sh'), os.path.join('tools', 'build_cache.sh')]))
        self.assertEqual(self.files(self.gate_calls(root)[0][-1]), staged, "not the gate's narrowed copy")
        # A file added or removed outside the sketch leaves the staged files as they
        # were, so the next build stays incremental.
        put(os.path.join(root, 'test', 'test_new.cpp'))
        put(os.path.join(root, 'docs', 'new.md'))
        os.remove(os.path.join(root, 'sim', 'x.cpp'))
        _, call = self.standalone(root)
        self.assertEqual(call[-1], stage, 'the staged copy moved')
        self.assertEqual(self.files(stage), staged)
        # One in the sketch arrives, uncommitted as it is, and a deleted one goes.
        put(os.path.join(root, 'new.cpp'))
        os.remove(os.path.join(root, 'pet.cpp'))
        self.standalone(root)
        self.assertEqual(self.files(stage), sorted(set(staged) - {'pet.cpp'} | {'new.cpp'}))

    def test_flash_and_update_use_the_image_a_standalone_build_wrote(self):
        # Without --out (and TAMAPOKE_OUT), build_175c.sh writes the image to the
        # checkout's ../out, where flash_175c.sh and update_175c.sh read it,
        # whatever the folder it built from. A fake esptool records what it would
        # write, so no board is touched.
        work = os.path.join(self.tmp, 'work')
        root = os.path.join(work, 'agent-worktree')
        shutil.copytree(TOOLS, os.path.join(root, 'tools'), ignore=shutil.ignore_patterns('build', '__pycache__'))
        shutil.copytree(os.path.join(ROOT, 'boards'), os.path.join(root, 'boards'))
        put(os.path.join(root, 'TamaPoke.ino'), b'void setup() {}\nvoid loop() {}\n')
        put(os.path.join(root, 'tools', 'board.py'), b'print("clock set")\n')   # stands in for the board
        esplog = os.path.join(self.tmp, 'esptool.log')
        esptool = os.path.join(self.bindir, 'esptool')
        put(esptool, (f'#!/usr/bin/env bash\necho "$*" >> "{esplog}"\nargs=("$@")\n'
                      'for i in "${!args[@]}"; do\n'
                      '  if [[ "${args[$i]}" == read-flash ]]; then head -c $(( ${args[$((i+2))]} )) /dev/zero > "${args[$((i+3))]}"; fi\n'
                      'done\n').encode())
        put(os.path.join(self.bindir, 'sleep'), b'#!/bin/sh\nexit 0\n')
        for tool in ('esptool', 'sleep'):
            os.chmod(os.path.join(self.bindir, tool), 0o755)
        port = os.path.join(self.tmp, 'fake-port')
        put(port)
        env = dict(self.env, ESPTOOL=esptool, TAMAPOKE_BACKUP_DIR=os.path.join(self.tmp, 'backups'))
        image = os.path.join(work, 'out', 'TamaPoke.ino.bin')

        def run(script, *args):
            if os.path.exists(esplog):
                os.remove(esplog)
            self.fresh()
            r = subprocess.run([os.path.join(root, 'tools', script), *args], env=env,
                               capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, f'{script}: {r.stdout}{r.stderr}')
            if not os.path.exists(esplog):
                return r, []
            with open(esplog) as fh:
                return r, [line.split() for line in fh if 'write-flash' in line]

        def flashed(writes):
            (write,) = writes
            self.assertIn('0x10000', write)
            return [os.path.normpath(a) for a in write if a.endswith('.bin')]

        run('build_175c.sh')
        self.assertTrue(os.path.isfile(image), 'the build did not write the image to ../out')
        _, writes = run('flash_175c.sh', '--port', port, '--app-only')
        self.assertEqual(flashed(writes), [image])
        # The update builds from scratch and flashes what it built; and the
        # player's update shows what a prune freed (here a removed checkout's folder).
        os.remove(image)
        gone = os.path.join(self.home, '.cache', 'tamapoke', 'gone-0123456789ab')
        os.makedirs(gone)
        put(os.path.join(gone, 'checkout'), os.path.join(self.tmp, 'gone').encode() + b'\n')
        r, writes = run('update_175c.sh', '--port', port)
        calls = self.calls()
        self.assertEqual(len(calls), 1, calls)
        call = calls[0]
        self.assertIn('--clean', call)
        self.assertEqual(os.path.normpath(self.opt(call, '--output-dir')), os.path.dirname(image))
        self.assertEqual(flashed(writes), [image])
        self.assertIn(f"Build cache: deleted the folder of {os.path.join(self.tmp, 'gone')}", r.stdout)

    def test_the_gate_stages_into_one_folder_that_mirrors_the_tree(self):
        root = self.gate_root()
        put(os.path.join(root, 'gone.cpp'))
        put(os.path.join(root, 'kept.h'))
        os.utime(os.path.join(root, 'kept.h'), (1577836800, 1577836800))   # 2020-01-01
        c175c, c175 = self.gate_calls(root)
        stage = c175[-1]
        self.assertEqual(c175c[-1], stage, 'both boards build the same staged copy')
        self.assertEqual(os.path.basename(stage), 'TamaPoke')
        gate_dir = os.path.dirname(os.path.dirname(stage))
        self.assertEqual(self.opt(c175c, '--build-path'), os.path.join(gate_dir, 'build-175c'))
        self.assertEqual(self.opt(c175, '--build-path'), os.path.join(gate_dir, 'build-175'))
        self.assertTrue(os.path.exists(os.path.join(stage, 'gone.cpp')))
        # Only what the firmware build reads: arduino-cli rebuilds everything when
        # the set of files anywhere under the sketch changes, so a new test must
        # not reach the staged copy.
        self.assertEqual(sorted(os.listdir(os.path.join(stage, 'tools'))), ['build_175c.sh', 'build_cache.sh'])
        for folder in ('test', 'sim'):
            self.assertFalse(os.path.exists(os.path.join(stage, folder)), f'{folder}/ was staged')
        self.assertTrue(os.path.exists(os.path.join(stage, 'boards', '175c', 'partitions.csv')))
        os.remove(os.path.join(root, 'gone.cpp'))
        put(os.path.join(root, 'new.cpp'))
        c175c, c175 = self.gate_calls(root)
        self.assertEqual([c175c[-1], c175[-1]], [stage, stage], 'the staged copy moved')
        self.assertFalse(os.path.exists(os.path.join(stage, 'gone.cpp')), 'a deleted file is still staged')
        self.assertTrue(os.path.exists(os.path.join(stage, 'new.cpp')))
        self.assertEqual(os.stat(os.path.join(stage, 'kept.h')).st_mtime,
                         os.stat(os.path.join(root, 'kept.h')).st_mtime, 'staging changed an mtime')

    def test_the_gate_stages_a_same_size_edit_even_when_the_mtime_did_not_change(self):
        # rsync's size-and-mtime quick check skips such a file; the stage now
        # persists, so the stale copy would stay for good. os.utime stands in
        # for an edit within the same second as the previous sync.
        root = self.gate_root()
        src = os.path.join(root, 'same.h')
        put(src, b'aaaa')
        os.utime(src, (1577836800, 1577836800))
        stage = self.gate_calls(root)[0][-1]
        with open(os.path.join(stage, 'same.h'), 'rb') as fh:
            self.assertEqual(fh.read(), b'aaaa')
        put(src, b'bbbb')
        os.utime(src, (1577836800, 1577836800))
        self.gate_calls(root)
        with open(os.path.join(stage, 'same.h'), 'rb') as fh:
            self.assertEqual(fh.read(), b'bbbb', 'the same-size edit never reached the staged copy')

    def test_a_partition_table_an_earlier_build_left_is_not_kept(self):
        # The core's prebuild hooks copy a scheme's table only if the build
        # folder has none, and arduino-cli ignores .csv files: a leftover
        # would be used for ever.
        root = self.gate_root()
        c175c, c175 = self.gate_calls(root)
        b175c, b175 = self.opt(c175c, '--build-path'), self.opt(c175, '--build-path')
        self.assertTrue(os.path.exists(os.path.join(b175c, 'partitions.csv')), 'the 1.75C build got no table')
        self.assertFalse(os.path.exists(os.path.join(b175, 'partitions.csv')), 'the fake wrote a table for the 1.75')
        for build in (b175c, b175):
            put(os.path.join(build, 'partitions.csv'), b'# stale\nnvs,data,nvs,0x9000,0x5000,\n')
        self.gate_calls(root)
        self.assertFalse(os.path.exists(os.path.join(b175, 'partitions.csv')), 'the 1.75 build kept a stale table')
        with open(os.path.join(b175c, 'partitions.csv')) as fh:
            self.assertNotIn('stale', fh.read(), 'the 1.75C build kept a stale table')

    def test_the_gate_fails_on_sketch_warnings_over_the_baseline(self):
        root = self.gate_root()
        for n, env, green in ((17, {}, True), (18, {}, False), (21, {'WARN_BASELINE': '21'}, True)):
            with self.subTest(n=n, env=env):
                self.fresh()
                r = self.gate(root, FAKE_WARNINGS=str(n), **env)
                base = env.get('WARN_BASELINE', '17')
                self.assertEqual(r.returncode == 0, green, r.stdout + r.stderr)
                for label in ('1.75C', '1.75'):
                    self.assertIn(f'{label}: {n} sketch warning(s), baseline {base}', r.stdout)
                    if green:
                        self.assertIn(f'PASS  {label} build  ({n} sketch warnings)', r.stdout)
                    else:
                        self.assertIn(f'FAIL  {label} build (exit 1)  ({n} sketch warnings: +1 NEW over baseline {base})',
                                      r.stdout)

    def test_gate_refuses_a_bad_warning_baseline_before_running_anything(self):
        root = self.gate_root()
        for bad in ('x', '-1', '08', '010', '12345', '99999999999999999999'):
            with self.subTest(bad=bad):
                self.fresh()
                r = self.gate(root, FAKE_ORDER_LOG=self.ran, WARN_BASELINE=bad)
                self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
                self.assertIn('WARN_BASELINE', r.stderr)
                self.assertFalse(os.path.exists(self.ran), 'the gate ran steps before checking WARN_BASELINE')
                self.assertEqual(self.calls(), [])
        # No firmware builds, nothing to count: the value does not matter.
        for flag in ('--no-build', '--no-firmware'):
            with self.subTest(flag=flag):
                self.fresh()
                r = self.gate(root, flag, WARN_BASELINE='08')
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                self.assertEqual(self.calls(), [])

    def test_no_firmware_runs_everything_but_the_firmware_builds(self):
        root = self.gate_root()
        r = self.gate(root, '--no-firmware', FAKE_ORDER_LOG=self.ran)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(self.ran) as fh:
            self.assertEqual(fh.read().split(), ['tests', 'i18n', 'sim', 'sim'])
        self.assertEqual(self.calls(), [])
        for line in ('PASS  host tests', 'PASS  i18n formats', 'PASS  no-network',
                     'PASS  simulator 175c', 'PASS  simulator 175', 'SKIP  firmware builds'):
            self.assertIn(line, r.stdout)
        self.assertNotIn('SKIP  simulator', r.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.home, '.cache', 'tamapoke')),
                         'took a build folder it never builds in')
        # no firmware build: the job count does not matter, --clean has nothing to clean
        self.fresh()
        r = self.gate(root, '--no-firmware', TAMAPOKE_JOBS='08')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        r = self.gate(root, '--no-firmware', '--clean')
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn('--clean', r.stderr)
        r = subprocess.run([os.path.join(TOOLS, 'gate.sh'), '--help'], env=self.env,
                           capture_output=True, text=True, timeout=30)
        self.assertIn('--no-firmware', r.stdout)

    def test_the_window_selftest_runs_for_both_boards_when_sdl2_works(self):
        root = self.gate_root()
        r = self.gate(root, '--no-firmware', FAKE_ORDER_LOG=self.ran, FAKE_SDL='1')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(self.ran) as fh:
            self.assertEqual(fh.read().split(), ['tests', 'i18n', 'sim', 'sim', 'window', 'window'])
        for line in ('PASS  window 175c  (selftest ok, 0 host build warning(s))',
                     'PASS  window 175  (selftest ok, 0 host build warning(s))'):
            self.assertIn(line, r.stdout)
        self.assertNotIn('SKIP  window', r.stdout)

    def test_the_window_is_skipped_when_there_is_no_sdl2(self):
        root = self.gate_root()
        r = self.gate(root, '--no-firmware', FAKE_ORDER_LOG=self.ran)  # the stand-in sdl2-config fails
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(self.ran) as fh:
            self.assertEqual(fh.read().split(), ['tests', 'i18n', 'sim', 'sim'])
        self.assertIn('SKIP  window (no SDL2)', r.stdout)
        self.assertNotIn('window 175', r.stdout)
        # --no-build skips the whole simulator, the window with it
        self.fresh()
        r = self.gate(root, '--no-build', FAKE_SDL='1')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('SKIP  simulator', r.stdout)
        self.assertNotIn('window', r.stdout.replace('SKIP  simulator', ''))

    def test_sim_window_without_sdl2_says_what_is_missing_and_builds_nothing(self):
        # the stand-in sdl2-config fails unless FAKE_SDL=1: this machine "has no SDL2"
        r = subprocess.run([os.path.join(TOOLS, 'sim.sh'), '--window', '--selftest'], env=self.env,
                           capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn('sim.sh: --window needs SDL2', r.stderr)
        self.assertIn('Ask the owner before installing', r.stderr)
        self.assertNotIn('build failed', r.stderr)   # it stopped before building anything
        self.assertEqual(self.calls(), [])

    def test_a_failing_or_silent_window_selftest_fails_the_gate(self):
        root = self.gate_root()
        for mode in ('fail', 'exit1', 'silent'):
            with self.subTest(mode=mode):
                self.fresh()
                r = self.gate(root, '--no-firmware', FAKE_ORDER_LOG=self.ran, FAKE_SDL='1', FAKE_WINDOW=mode)
                self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
                self.assertIn('FAIL  window 175c', r.stdout)
                self.assertIn('FAIL  window 175 ', r.stdout)
                self.assertIn('GATE FAILED', r.stdout)

    def test_the_gate_runs_the_fast_steps_before_the_firmware_builds(self):
        root = self.gate_root()
        r = self.gate(root, FAKE_ORDER_LOG=self.ran)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(self.ran) as fh:
            self.assertEqual(fh.read().split(), ['tests', 'i18n', 'sim', 'sim', 'compile', 'compile'])

    def test_the_app0_cap_holds_on_every_run(self):
        build = self.build_path()
        self.assertTrue(os.path.exists(os.path.join(self.tmp, 'out', 'TamaPoke.ino.bin')))
        # a rebuild in the same folder: arduino-cli gets the cap, and the image is checked
        for size, ok in ((self.APP0, True), (self.APP0 + 1, False)):
            with self.subTest(size=size):
                self.fresh()
                r = self.build_175c(FAKE_BIN_SIZE=str(size))
                self.assertEqual(r.returncode == 0, ok, r.stderr)
                (call,) = self.calls()
                self.assertEqual(self.opt(call, '--build-path'), build)
                self.assertIn(f'upload.maximum_size={self.APP0}', call)
                if not ok:
                    self.assertIn('larger than app0', r.stderr)
        # what is checked is this run's output, never a leftover from an earlier run
        put(os.path.join(self.tmp, 'out', 'TamaPoke.ino.bin'), b'\0' * 1024)
        r = self.build_175c(FAKE_NO_BIN='1')
        self.assertNotEqual(r.returncode, 0, 'passed on an image an earlier build left')
        self.assertIn('wrote no', r.stderr)
        self.assertTrue(os.path.exists(os.path.join(build, 'partitions.csv')), 'expected a leftover table')
        r = self.build_175c(FAKE_PARTS='')
        self.assertNotEqual(r.returncode, 0, 'passed on a partition table an earlier build left')
        self.assertIn('wrong partition table', r.stderr)


class TestGateHostWarnings(FakeArduinoCliCase):
    """The gate's host tests fail on a host build warning, counted as CI's "No host build
    warnings" step counts them (#98; TestSketchWarnings.test_ci_host_tests_fail_on_a_warning
    runs that step on the same kinds of log): each different ': warning: ' line once, colour
    codes stripped, and the count on the summary line."""

    LINE = '../a.cpp:1:2: warning: x [-Wfoo]\n'
    OTHER = '../b.cpp:3:4: warning: y [-Wbar]\n'
    COLOURED = '../a.cpp:1:2: \033[01;35m\033[Kwarning: \033[m\033[Kx\n'
    # the suite's own output says "warning" in test names: not a compiler warning
    NAMES = ('  ok   run_away_warning_sounds_once_each_time_the_button_appears\n'
             'test_ci_host_tests_fail_on_a_warning (__main__.TestSketchWarnings) ... ok\n')

    def gate_with(self, text, **env):
        root = getattr(self, 'root', None) or self.gate_root()
        self.root = root
        log = os.path.join(self.tmp, 'suite.log')
        put(log, text.encode())
        return self.gate(root, '--no-build', FAKE_TEST_LOG=log, **env)

    def test_a_clean_suite_passes_and_says_none(self):
        r = self.gate_with(self.NAMES)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('PASS  host tests (--asan)  (0 host build warning(s))', r.stdout)
        self.assertIn('GATE GREEN', r.stdout)

    def test_a_host_build_warning_fails_the_gate_counted_once_per_line(self):
        for name, text, n in (('once', self.NAMES + self.LINE, 1),
                              # the normal and the ASan build print the same line
                              ('twice', self.LINE + self.NAMES + self.LINE, 1),
                              ('two different', self.LINE + self.OTHER + self.LINE, 2),
                              ('coloured', self.COLOURED, 1)):
            with self.subTest(log=name):
                self.fresh()
                r = self.gate_with(text)
                self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
                self.assertIn(f'FAIL  host tests (--asan) (exit 1)  ({n} host build warning(s))', r.stdout)
                self.assertIn('GATE FAILED', r.stdout)

    def test_a_failing_suite_keeps_its_own_exit_code(self):
        r = self.gate_with(self.LINE, FAKE_TEST_RC='3')
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn('FAIL  host tests (--asan) (exit 3)  (1 host build warning(s))', r.stdout)


class TestGateSimChecks(FakeArduinoCliCase):
    """The gate's "sim checks" step (#99): every simulator script with an expect or
    expect-not step runs on both boards, and fails the gate unless it exits 0 and reports
    as many passed checks as it has check steps. The stand-in simulator is gate_root's."""

    def scripts(self, root, **texts):
        for name, text in texts.items():
            put(os.path.join(root, 'sim', 'scripts', name + '.txt'), text.encode())

    def checks_root(self):
        root = self.gate_root()
        self.scripts(root,
                     a_game='wait 1s\nexpect Play: song untouched (screen off)\nexpect-not Play: keepy\n',
                     b_train='  expect-not Training:\n# expect in a comment is not a check\nwait 1s\n'
                             'expect\tTRAIN: asleep\nexpect-once Training: punch\n',
                     no_checks='wait 1s\nshot x\n# expect nothing here\nserial expect\n')
        return root

    def order(self):
        """What the gate ran, in order (gate_root's stand-ins write it to self.ran)."""
        with open(self.ran) as fh:
            return fh.read().split()

    def test_every_script_with_a_check_runs_on_both_boards(self):
        root = self.checks_root()
        r = self.gate(root, '--no-firmware')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.order(), ['tests', 'i18n', 'sim', 'sim',
                                      'check-175c-a_game', 'check-175c-b_train',
                                      'check-175-a_game', 'check-175-b_train'])
        self.assertIn('PASS  sim checks 175c  (2 scripts, 5/5 checks)', r.stdout)
        self.assertIn('PASS  sim checks 175  (2 scripts, 5/5 checks)', r.stdout)
        self.assertNotIn('SKIP  sim checks', r.stdout)
        # the script's own `lang` step decides: the gate passes no --lang
        with open(self.args) as fh:
            for line in fh:
                self.assertNotIn('--lang', line)
                self.assertIn('--quiet', line)

    def test_one_script_with_a_check_is_reported_in_the_singular(self):
        root = self.gate_root()
        self.scripts(root, only_game='wait 1s\nexpect Play: song untouched (screen off)\n',
                     no_checks='wait 1s\nshot x\n')
        r = self.gate(root, '--no-firmware')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('PASS  sim checks 175c  (1 script, 1/1 checks)', r.stdout)
        self.assertIn('PASS  sim checks 175  (1 script, 1/1 checks)', r.stdout)
        self.assertNotIn('1 scripts', r.stdout)

    def test_the_whole_gate_runs_the_checks_before_the_firmware_builds(self):
        root = self.checks_root()
        r = self.gate(root, FAKE_ORDER_LOG=self.ran)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(self.order(), ['tests', 'i18n', 'sim', 'sim',
                                      'check-175c-a_game', 'check-175c-b_train',
                                      'check-175-a_game', 'check-175-b_train', 'compile', 'compile'])

    def test_a_failing_check_fails_the_gate_and_names_the_script(self):
        root = self.checks_root()
        r = self.gate(root, '--no-firmware', FAKE_CHECKS='fail', FAKE_CHECKS_SCRIPT='b_train')
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn('FAIL  sim checks 175c (exit 1)  (4/5 checks; failed: b_train)', r.stdout)
        self.assertIn('FAIL  sim checks 175 (exit 1)  (4/5 checks; failed: b_train)', r.stdout)
        self.assertIn('b_train.txt:2: expect failed: stub', r.stdout)   # what the simulator said
        self.assertIn('GATE FAILED', r.stdout)
        # the other script still ran on both boards
        self.assertEqual([x for x in self.order() if x.startswith('check-')],
                         ['check-175c-a_game', 'check-175c-b_train', 'check-175-a_game', 'check-175-b_train'])

    def test_a_run_that_does_not_report_every_check_passed_fails_the_gate(self):
        root = self.checks_root()
        for mode in ('exit1', 'short', 'silent', 'extra'):
            with self.subTest(mode=mode):
                self.fresh()
                r = self.gate(root, '--no-firmware', FAKE_CHECKS=mode, FAKE_CHECKS_SCRIPT='a_game')
                self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
                self.assertIn('FAIL  sim checks 175c', r.stdout)
                self.assertIn('FAIL  sim checks 175 ', r.stdout)
                self.assertIn('failed: a_game)', r.stdout)
                self.assertIn('GATE FAILED', r.stdout)

    def test_no_script_with_a_check_is_a_skip_line(self):
        root = self.gate_root()
        self.scripts(root, no_checks='wait 1s\n# expect nothing\nserial expect\n')
        r = self.gate(root, '--no-firmware')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('SKIP  sim checks (no script in sim/scripts has an expect step)', r.stdout)
        self.assertEqual(self.order(), ['tests', 'i18n', 'sim', 'sim'])

    def test_no_build_skips_the_checks_with_the_simulator(self):
        root = self.checks_root()
        r = self.gate(root, '--no-build')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('SKIP  simulator', r.stdout)
        self.assertNotIn('sim checks', r.stdout)
        self.assertEqual(self.order(), ['tests', 'i18n'])


class TestBoardPy(unittest.TestCase):
    def setUp(self):
        import board
        self.board = board
        old = os.environ.get('TZ')

        def restore():
            if old is None:
                os.environ.pop('TZ', None)
            else:
                os.environ['TZ'] = old
            time.tzset()
        self.addCleanup(restore)
        self.set_tz('AWST-8')   # a zone that is never UTC, so a wrong epoch shows up even on a UTC CI host

    @staticmethod
    def set_tz(tz):
        os.environ['TZ'] = tz
        time.tzset()

    def test_wallclock_epoch_is_local_time_written_as_utc(self):
        # The firmware shows the epoch as e/3600 % 24 (and Pet::today() is
        # e/86400), so the epoch's UTC fields must equal this computer's local
        # fields: the true epoch plus the zone's offset.
        now = 1_794_562_514
        ep = self.board.local_wallclock_epoch(now)
        self.assertEqual(ep, now + 8 * 3600)
        self.assertEqual(time.gmtime(ep)[:6], time.localtime(now)[:6])

    def test_wallclock_epoch_follows_daylight_saving(self):
        self.set_tz('EST5EDT,M3.2.0,M11.1.0')
        for now, offset in ((1_783_000_000, -4 * 3600), (1_794_562_514, -5 * 3600)):
            with self.subTest(now=now):
                self.assertEqual(self.board.local_wallclock_epoch(now), now + offset)

    def test_parse_rtc(self):
        self.assertEqual(self.board.parse_rtc(['x', 'rtc=1794562514']), 1794562514)
        self.assertIsNone(self.board.parse_rtc(['spec=4 rtc=5']))
        self.assertIsNone(self.board.parse_rtc(['rtc=abc']))

    def test_a_command_that_never_prints_done_ends_when_the_reply_goes_quiet(self):
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)

        def board_side():
            os.read(master, 64)                      # the command line
            os.write(master, b'one\ntwo\n')           # ...and no DONE
        t = threading.Thread(target=board_side, daemon=True)
        t.start()
        con = self.board.Console(os.ttyname(slave))
        self.addCleanup(con.close)
        seen = []
        started = time.time()
        lines = con.command('LOCK?', timeout=8, idle=0.5, on_line=seen.append)
        t.join()
        self.assertEqual(lines, ['one', 'two'])
        self.assertEqual(seen, ['one', 'two'])
        self.assertLess(time.time() - started, 4, 'waited for the full timeout')

    def run_cmd(self, board_side, *line):
        import contextlib
        import io
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        threading.Thread(target=board_side, args=(master,), daemon=True).start()
        out = io.StringIO()
        started = time.time()
        with contextlib.redirect_stdout(out):
            self.board.main(['--port', os.ttyname(slave), 'cmd', *line])
        return out.getvalue().split(), time.time() - started

    def test_cmd_lock_query_ends_without_done(self):
        def board_side(master):
            os.read(master, 64)
            os.write(master, b'LOCK locked=0 panel_sleep=0\n')
        out, took = self.run_cmd(board_side, 'LOCK?')
        self.assertIn('locked=0', out)
        self.assertLess(took, 8, 'waited for the full timeout')

    def test_cmd_keeps_a_slow_reply_after_an_unsolicited_line(self):
        # The HEALTH heartbeat can land just before a reply that follows a ~1 s NVS write.
        def board_side(master):
            os.read(master, 64)
            os.write(master, b'HEALTH up=1s\n')
            time.sleep(1.5)
            os.write(master, b'saved\nDONE\n')
        out, _ = self.run_cmd(board_side, 'SAVE')
        self.assertIn('saved', out, 'the reply was cut off by the quiet-period logic')

    def test_cmd_waits_for_a_reply_that_starts_late(self):
        def board_side(master):
            os.read(master, 64)
            time.sleep(3.5)          # longer than the quiet period, before any output
            os.write(master, b'saved\nDONE\n')
        out, _ = self.run_cmd(board_side, 'SAVE')
        self.assertIn('saved', out, 'gave up before the reply started')

    def test_a_command_with_no_reply_says_so(self):
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        con = self.board.Console(os.ttyname(slave))
        self.addCleanup(con.close)
        with self.assertRaises(self.board.NoReply) as cm:
            con.command('FOO', timeout=1, idle=3.0)
        self.assertIn('unknown command', str(cm.exception))

    def test_write_sends_all_of_a_long_write(self):
        # The port is non-blocking: one os.write takes only what fits in the tty's buffer, so a
        # long write (send_sd.py's 2 KB blocks; far more here) has to wait for room for the rest.
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        data = bytes(range(256)) * 800                  # 200 KB
        got = []

        def board_side():
            import select
            while select.select([master], [], [], 1.0)[0]:
                got.append(os.read(master, 4096))
                time.sleep(0.001)                       # a board slower than the computer
        t = threading.Thread(target=board_side, daemon=True)
        t.start()
        con = self.board.Console(os.ttyname(slave))
        self.addCleanup(con.close)
        con.write(data, timeout=20)
        t.join(25)
        self.assertEqual(len(b''.join(got)), len(data))
        self.assertEqual(b''.join(got), data)

    def test_read_line_skips_blank_lines_and_gives_none_when_nothing_comes(self):
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        con = self.board.Console(os.ttyname(slave))
        self.addCleanup(con.close)
        os.write(master, b'\r\n\r\nOK\r\n#')
        self.assertEqual(con.read_line(2), 'OK')
        self.assertIsNone(con.read_line(0.3), 'a line with no end yet is not a line')
        os.write(master, b'\r\n')
        self.assertEqual(con.read_line(2), '#')

    def test_cmd_sends_accents_precomposed_and_keeps_the_spaces(self):
        # OWNER <name> (#23): a name typed with a combining accent (as some macOS
        # input gives it) reaches the board as the precomposed letter the firmware
        # accepts; the words are joined with single spaces, a quoted name keeps its own
        got = []

        def board_side(master):
            got.append(os.read(master, 256))
            os.write(master, b'DONE\n')
        self.run_cmd(board_side, 'OWNER', 'N\u0303andu\u0301', 'Player')
        self.assertEqual(got[0], 'OWNER \u00d1and\u00fa Player\n'.encode('utf-8'))
        got.clear()
        self.run_cmd(board_side, 'OWNER', 'Pla  yer')
        self.assertEqual(got[0], b'OWNER Pla  yer\n')

    def test_an_unplugged_board_gives_a_message_not_a_traceback(self):
        for port in ('/dev/does-not-exist', __file__):   # missing, and not a tty
            with self.subTest(port=port):
                r = subprocess.run([sys.executable, '-I', os.path.join(TOOLS, 'board.py'),
                                    '--port', port, 'status'],
                                   capture_output=True, text=True, timeout=30)
                self.assertNotEqual(r.returncode, 0)
                self.assertNotIn('Traceback', r.stderr)
                self.assertIn('board.py:', r.stderr)

    def run_clock(self, refusal):
        """board.py clock against a board whose clock reads 2026-11-13 10:00 and does
        not take the time sent; `refusal` is what it prints before rtc= (#45)."""
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        sent = []

        def board_side():
            os.read(master, 64)                                   # TIME
            os.write(master, b'rtc=1794564000\nDONE\n')
            sent.append(os.read(master, 64))                      # TIME <now>
            os.write(master, refusal + b'rtc=1794564000\nDONE\n')
        t = threading.Thread(target=board_side, daemon=True)
        t.start()
        with self.assertRaises(SystemExit) as cm:
            self.board.main(['--port', os.ttyname(slave), 'clock'])
        t.join(5)
        self.assertTrue(sent and sent[0].startswith(b'TIME '), sent)
        self.assertIsInstance(cm.exception.code, str, 'a refused TIME must exit non-zero')
        return cm.exception.code

    def test_clock_fails_with_the_boards_reason_when_the_clock_refuses_the_time(self):
        msg = self.run_clock(b"ERR clock not set: it did not take 5; "
                             b"the pet's saved time is unchanged\n")
        self.assertIn("ERR clock not set: it did not take 5; the pet's saved time is unchanged", msg)

    def test_clock_fails_when_an_older_firmware_reads_back_another_time(self):
        # firmware from before #45 prints no ERR line: the rtc= it reads back gives it away
        msg = self.run_clock(b'')
        self.assertIn('clock not set: board answered rtc=1794564000', msg)

    # The battery diary (#14): the lines test/test_batdiary.cpp's
    # the_dump_lists_the_rows_oldest_first checks the firmware prints for BATLOG?.
    BATLOG_REPLY = [
        'BATLOG rows=3 cap=96 unsaved=1 period_min=60',
        'epoch,event,pct,mv,usb,chg,on_min,sleep_min',
        '0,boot,90,4110,0,0,0,0',
        '1794564000,hour,84,4056,0,0,5,50',
        '1794567600,hour,79,4011,0,0,0,0',
    ]

    def test_parse_batlog_reads_the_firmwares_dump(self):
        rows, info = self.board.parse_batlog(['HEALTH up=1s', *self.BATLOG_REPLY])  # a heartbeat first
        self.assertEqual(info, {'rows': '3', 'cap': '96', 'unsaved': '1', 'period_min': '60'})
        self.assertEqual([r['event'] for r in rows], ['boot', 'hour', 'hour'])
        self.assertEqual(rows[1], {'epoch': 1794564000, 'event': 'hour', 'pct': 84, 'mv': 4056,
                                   'usb': False, 'charging': False, 'on_min': 5, 'asleep_min': 50})

    def test_parse_batlog_refuses_a_dump_cut_short(self):
        # the firmware stops the dump when nobody takes the bytes: the count gives it away
        with self.assertRaises(self.board.BatlogError) as cm:
            self.board.parse_batlog(self.BATLOG_REPLY[:-1])
        self.assertIn('cut short: got 2 of 3 rows', str(cm.exception))
        with self.assertRaises(self.board.BatlogError):
            self.board.parse_batlog(self.BATLOG_REPLY + ['1794571200,hour,70,3990,0,0,0,0'])

    def test_parse_batlog_refuses_a_reply_that_is_not_a_diary(self):
        for lines in (['rtc=1'], self.BATLOG_REPLY[:1] + ['epoch,event'] + self.BATLOG_REPLY[2:],
                      self.BATLOG_REPLY[:2] + ['0,boot,90,4110,0,0,0'] + self.BATLOG_REPLY[3:],
                      self.BATLOG_REPLY[:2] + ['0,boot,x,4110,0,0,0,0'] + self.BATLOG_REPLY[3:],
                      ['BATLOG cap=96'] + self.BATLOG_REPLY[1:]):
            with self.subTest(lines=lines[:3]), self.assertRaises(self.board.BatlogError):
                self.board.parse_batlog(lines)

    def test_parse_batlog_refuses_a_row_damaged_on_the_way(self):
        # HWCDC drops bytes when its link flaps (#97 review R2-2): a row that lost bytes inside
        # it still has 8 integer fields, so each field is checked against what the firmware writes
        good = '1794564000,hour,84,4056,0,0,5,50'
        cases = {
            'epoch not a minute': '1794564030,hour,84,4056,0,0,5,50',
            'epoch before 2025': '17945640,hour,84,4056,0,0,5,50',
            'epoch from 2100 on': '4102444800,hour,84,4056,0,0,5,50',
            'unknown event': '1794564000,our,84,4056,0,0,5,50',
            'pct over 100': '1794564000,hour,184,4056,0,0,5,50',
            'pct under -1': '1794564000,hour,-4,4056,0,0,5,50',
            'mv over 5000': '1794564000,hour,84,40560,0,0,5,50',
            'usb not 0/1': '1794564000,hour,84,4056,10,0,5,50',
            'chg not 0/1': '1794564000,hour,84,4056,0,2,5,50',
            'on over 60': '1794564000,hour,84,4056,0,0,65,50',
            'asleep over 60': '1794564000,hour,84,4056,0,0,5,500',
        }
        head = ['BATLOG rows=1 cap=96 unsaved=0 period_min=60', self.BATLOG_REPLY[1]]
        rows, _ = self.board.parse_batlog(head + [good])
        self.assertEqual(len(rows), 1)
        rows, _ = self.board.parse_batlog(head + ['0,cutoff,-1,0,1,1,60,60'])  # every limit itself
        self.assertEqual(rows[0]['event'], 'cutoff')
        for why, text in cases.items():
            with self.subTest(why), self.assertRaises(self.board.BatlogError) as cm:
                self.board.parse_batlog(head + [text])
            self.assertIn('damaged', str(cm.exception))

    def test_batlog_rows_become_the_csv_columns(self):
        row = lambda **kw: {'epoch': 1794564000, 'event': 'hour', 'pct': 84, 'mv': 4056, 'usb': False,
                            'charging': False, 'on_min': 5, 'asleep_min': 50, **kw}
        self.assertEqual(self.board.batlog_csv_row(row()),
                         ['2026-11-13 10:00', 'hour', 84, 4056, 'discharging', 5, 50])
        self.assertEqual(self.board.batlog_csv_row(row(usb=True, charging=True))[4], 'charging')
        self.assertEqual(self.board.batlog_csv_row(row(usb=True))[4], 'full')
        # no time (clock not set), no battery reading, no voltage: empty cells
        self.assertEqual(self.board.batlog_csv_row(row(epoch=0, pct=-1, mv=0, usb=True))[:5],
                         ['', 'hour', '', '', 'no battery'])

    def run_batlog(self, reply, timeout=None):
        """board.py batlog against a board that answers `reply`; (chunks sent, stdout, stderr, exit).

        The chunks are everything board.py wrote to the port: the first read, then whatever else
        is still waiting once it has returned (a second command would show up there)."""
        import contextlib
        import io
        import select
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        sent = []

        def board_side():
            sent.append(os.read(master, 64))
            if reply:
                os.write(master, reply)
        thread = threading.Thread(target=board_side, daemon=True)
        thread.start()
        if timeout is not None:
            old = self.board.BATLOG_TIMEOUT
            self.board.BATLOG_TIMEOUT = timeout
            self.addCleanup(setattr, self.board, 'BATLOG_TIMEOUT', old)
        out, err = io.StringIO(), io.StringIO()
        code = None
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                self.board.main(['--port', os.ttyname(slave), 'batlog'])
            except SystemExit as e:
                code = e.code
        thread.join(5)
        while select.select([master], [], [], 0.1)[0]:
            sent.append(os.read(master, 64))
        return sent, out.getvalue(), err.getvalue(), code

    def test_batlog_prints_csv_and_sends_only_the_read_only_command(self):
        reply = ('\r\n'.join(self.BATLOG_REPLY) + '\r\nDONE\r\n').encode()
        sent, out, err, code = self.run_batlog(reply)
        self.assertIsNone(code)
        self.assertEqual(sent, [b'BATLOG?\n'], 'batlog must send only the read-only BATLOG?')
        self.assertEqual(out.splitlines(), [
            'time,event,battery_pct,battery_mv,state,screen_on_min,asleep_min',
            ',boot,90,4110,discharging,0,0',
            '2026-11-13 10:00,hour,84,4056,discharging,5,50',
            '2026-11-13 11:00,hour,79,4011,discharging,0,0',
        ])
        self.assertIn('3 rows', err)
        self.assertIn('the newest 1 are not in flash yet', err)

    def test_batlog_says_when_the_dump_was_cut_short(self):
        reply = ('\n'.join(self.BATLOG_REPLY[:-1]) + '\nDONE\n').encode()
        _, out, _, code = self.run_batlog(reply)
        self.assertIsInstance(code, str, 'a cut dump must exit non-zero')
        self.assertIn('cut short', code)
        self.assertEqual(out, '', 'no partial CSV on stdout')

    def test_batlog_says_when_the_dump_was_cut_short_and_the_done_was_lost_with_it(self):
        # the firmware stops a dump the host does not take, and its DONE is lost with the rest:
        # the reply starts and never ends, which is still a dump cut short
        reply = ('\r\n'.join(self.BATLOG_REPLY[:-1]) + '\r\n').encode()
        _, out, _, code = self.run_batlog(reply, timeout=1)
        self.assertIsInstance(code, str, 'a cut dump must exit non-zero')
        self.assertIn('cut short', code)
        self.assertIn('got 2 of 3 rows', code)
        self.assertEqual(out, '', 'no partial CSV on stdout')

    def test_batlog_keeps_a_whole_dump_whose_done_was_lost(self):
        reply = ('\r\n'.join(self.BATLOG_REPLY) + '\r\n').encode()
        _, out, _, code = self.run_batlog(reply, timeout=1)
        self.assertIsNone(code)
        self.assertEqual(len(out.splitlines()), 4)

    def test_batlog_that_got_lines_but_no_battery_diary_is_not_called_cut_short(self):
        # a heartbeat and nothing else: not a diary at all, so say what happened, not "cut short"
        _, _, _, code = self.run_batlog(b'HEALTH up=1s\r\n', timeout=1)
        self.assertIsInstance(code, str)
        self.assertIn('no DONE', code)
        self.assertNotIn('cut short', code)

    def test_batlog_on_a_firmware_without_the_diary(self):
        _, _, _, code = self.run_batlog(b'', timeout=1)
        self.assertIsInstance(code, str)
        self.assertIn('older than the battery diary', code)
        self.assertIn('try again', code)


class FakeSdBoard:
    """The far side of a pty, answering SDINFO, LS and PUT as sdmon.cpp's sdSerialCommand does
    on the 1.75C (LittleFS, whole 4 KB blocks), with its files in a dict. Nothing here touches
    a real board.

    trouble maps a PUT path to (what, block): 'refuse' answers the PUT with ERR; 'err' takes
    that block and says ERR instead of acking it (a full or failing flash); 'silent' takes it
    and says nothing, then ERR once no data has come for STALL s (the firmware waits 5 s);
    'nodone' acks every block and never says DONE. chatter puts a HEALTH heartbeat before
    every answer and every ack."""
    STALL = 0.4

    def __init__(self, test, files=None, free=10 << 20, other=0, total=None, trouble=None,
                 chatter=False):
        import select
        self.select = select.select
        self.master, slave = os.openpty()
        test.addCleanup(os.close, slave)
        test.addCleanup(os.close, self.master)
        self.port = os.ttyname(slave)
        self.files = dict(files or {})
        self.other = other                      # bytes used outside mons/
        self.total = self.used() + free if total is None else total
        self.trouble = trouble or {}
        self.chatter = chatter
        self.commands = []
        self.err_at = None                      # when a PUT that stopped said ERR
        self.buf = b''
        threading.Thread(target=self.serve, daemon=True).start()

    def used(self):
        return self.other + sum(-(-len(d) // 4096) * 4096 for d in self.files.values())

    def fill(self, timeout):
        if not self.select([self.master], [], [], timeout)[0]:
            return False
        data = os.read(self.master, 65536)
        if not data:
            raise EOFError
        self.buf += data
        return True

    def readline(self):
        while b'\n' not in self.buf:
            self.fill(None)
        line, self.buf = self.buf.split(b'\n', 1)
        return line.decode().strip()

    def read_block(self, n):
        """Up to n bytes; fewer once nothing has come for STALL s."""
        while len(self.buf) < n and self.fill(self.STALL):
            pass
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def say(self, *lines):
        if self.chatter:
            lines = ('HEALTH up=1s',) + lines
        os.write(self.master, ''.join(t + '\r\n' for t in lines).encode())

    def serve(self):
        try:
            while True:
                line = self.readline()
                self.commands.append(line)
                if line == 'SDINFO':
                    self.say(f'total={self.total} used={self.used()}', 'DONE')
                elif line == 'LS':
                    self.say(*(f'{n} {len(d)}' for n, d in self.files.items()), 'DONE')
                elif line.startswith('PUT '):
                    self.put(line)
        except (EOFError, OSError, ValueError):
            pass

    def put(self, line):
        path, size = line[4:].rsplit(' ', 1)
        size = int(size)
        what, at = self.trouble.get(path, (None, None))
        if what == 'refuse' or not path.startswith('mons/'):
            self.say('ERR')
            return
        name = path[len('mons/'):]
        self.files.pop(name, None)              # PUT deletes the old copy first
        self.say('OK')
        got, block = b'', 0
        while len(got) < size:
            chunk = self.read_block(min(2048, size - len(got)))
            if not chunk:
                break                           # no data for a while: the firmware gives up
            got += chunk
            if what == 'err' and block == at:
                break
            if not (what == 'silent' and block == at):
                self.say('#')
            block += 1
        self.files[name] = got                  # what was written stays, whole or not
        if len(got) < size or what == 'err':
            self.err_at = time.time()
            self.say('ERR')
        elif what != 'nodone':
            self.say('DONE')

    def puts(self):
        return [c for c in self.commands if c.startswith('PUT ')]


class TestSendSd(unittest.TestCase):
    """tools/send_sd.py (#110) against FakeSdBoard: only the files named, only if they fit,
    one result per file."""

    def setUp(self):
        import send_sd
        self.sd = send_sd
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)
        for name, value in (('OK_TIMEOUT', 2), ('ACK_TIMEOUT', 2), ('DONE_TIMEOUT', 2),
                            ('RESYNC_TIMEOUT', 0.5)):
            self.patch(name, value)

    def patch(self, name, value):
        self.addCleanup(setattr, self.sd, name, getattr(self.sd, name))
        setattr(self.sd, name, value)

    def sprite(self, name, size, magic=b'TPK2'):
        """A file of `size` bytes in the temp folder, starting with `magic`; its path."""
        path = os.path.join(self.tmp, name)
        with open(path, 'wb') as fh:
            fh.write((magic + bytes(range(256)) * (size // 256 + 1))[:size])
        return path

    def run_send(self, *args, board=None):
        """send_sd.py's main(['--port', board's port, *args]); (exit code, stdout, stderr)."""
        import contextlib
        import io
        out, err = io.StringIO(), io.StringIO()
        code = 0
        argv = ['--port', board.port, *args] if board else list(args)
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                self.sd.main(argv)
            except SystemExit as e:
                code = e.code
        return code, out.getvalue(), err.getvalue()

    def test_sends_only_the_files_named(self):
        board = FakeSdBoard(self, files={'p151.bin': b'TPK2' + b'n' * 9000})
        a, b = self.sprite('ps151.bin', 9000), self.sprite('ps025.bin', 5000)
        code, out, _ = self.run_send(a, b, board=board)
        self.assertEqual(code, 0)
        self.assertEqual(board.commands, ['SDINFO', 'LS', 'PUT mons/ps151.bin 9000',
                                          'PUT mons/ps025.bin 5000'])
        with open(a, 'rb') as fh:
            self.assertEqual(board.files['ps151.bin'], fh.read())
        self.assertEqual(len(board.files['ps025.bin']), 5000)
        self.assertIn('ps151.bin: sent', out)
        self.assertIn('ps025.bin: sent', out)
        self.assertIn('All 2 sent.', out)

    def test_a_plain_name_is_a_file_in_tools_sdcard_mons(self):
        self.patch('MONS', self.tmp)
        self.sprite('ps151.bin', 3000)
        board = FakeSdBoard(self)
        code, out, _ = self.run_send('ps151.bin', board=board)
        self.assertEqual(code, 0, out)
        self.assertEqual(len(board.files['ps151.bin']), 3000)
        code, _, _ = self.run_send('--port', '/dev/does-not-exist', 'ps150.bin')
        self.assertIn('ps150.bin: no such file in tools/sdcard/mons/', code)

    def test_a_bare_run_refuses_with_usage(self):
        code, _, err = self.run_send()
        self.assertEqual(code, 2)
        self.assertIn('name the files to send', err)
        self.assertIn('usage:', err)
        code, _, err = self.run_send('--ls', 'ps151.bin')
        self.assertEqual(code, 2)
        self.assertIn('--ls takes no file names', err)

    def test_names_the_firmware_never_reads_and_bad_files_are_refused_before_the_port(self):
        self.assertEqual(self.sd.DEX_MAX, 151)
        for name, magic in (('p001.bin', b'TPK2'), ('ps151.bin', b'TPK2'), ('001.bin', b'TPK1'),
                            ('s151.bin', b'TPK1'), ('thumbs.bin', b'TPTH'), ('ps000.bin', None),
                            ('ps152.bin', None), ('p1.bin', None), ('x001.bin', None),
                            ('ps151', None), ('PS151.bin', None)):
            with self.subTest(name=name):
                self.assertEqual(self.sd.magic_for(name), magic)
        wrong = self.sprite('ps025.bin', 900, magic=b'TPK1')
        big = self.sprite('ps026.bin', self.sd.PUT_MAX + 1)
        os.mkdir(os.path.join(self.tmp, 'dup'))
        twice = os.path.join(self.tmp, 'dup', 'ps027.bin')
        shutil.copy(self.sprite('ps027.bin', 900), twice)
        code, out, _ = self.run_send('--port', '/dev/does-not-exist', 'pikachu.bin', wrong, big,
                                     os.path.join(self.tmp, 'ps027.bin'), twice,
                                     os.path.join(self.tmp, 'ps028.bin'))
        self.assertIsInstance(code, str)
        self.assertTrue(code.startswith('send_sd.py: nothing sent:'), code)  # not a port error
        self.assertIn("no file called 'pikachu.bin'", code)
        self.assertIn('ps025.bin: not a sprite file of its kind', code)
        self.assertIn('ps026.bin: over 4,096 KB', code)
        self.assertIn('ps027.bin is named twice', code)
        self.assertIn('ps028.bin: no such file', code)
        self.assertEqual(out, '')

    def test_space_is_counted_in_whole_blocks_with_room_to_spare(self):
        # A new file: whole 4 KB blocks of 4,032 data bytes (LittleFS keeps the file's block
        # list in them), and one block more. A replaced file gives back at least its data in
        # whole blocks. 64 KB stays free.
        block = 4096
        for size, blocks in ((1, 2), (4032, 2), (4033, 3), (30000, 9)):
            with self.subTest(size=size):
                self.assertEqual(self.sd.new_cost(size), blocks * block)
        for size, blocks in ((1, 1), (4096, 1), (4097, 2), (20000, 5)):
            with self.subTest(old=size):
                self.assertEqual(self.sd.old_freed(size), blocks * block)
        self.assertEqual(self.sd.FS_SPARE, 16 * block)

    def test_files_that_do_not_fit_are_not_sent(self):
        a = self.sprite('ps151.bin', 30000)
        board = FakeSdBoard(self, free=self.sd.new_cost(30000) + self.sd.FS_SPARE - 1)
        code, out, _ = self.run_send(a, board=board)
        self.assertIsInstance(code, str)
        self.assertIn('not enough room, nothing sent', code)
        self.assertEqual(board.puts(), [])
        board = FakeSdBoard(self, free=self.sd.new_cost(30000) + self.sd.FS_SPARE)
        code, out, _ = self.run_send(a, board=board)
        self.assertEqual(code, 0, out)

    def test_a_file_the_board_already_has_counts_as_freed(self):
        old = b'TPK2' + b'o' * 19996                          # 20,000 bytes: 5 blocks
        a = self.sprite('ps151.bin', 20000)
        free = self.sd.new_cost(20000) - self.sd.old_freed(20000) + self.sd.FS_SPARE
        self.assertLess(free, self.sd.new_cost(20000) + self.sd.FS_SPARE)
        board = FakeSdBoard(self, files={'ps151.bin': old}, free=free)
        code, out, _ = self.run_send(a, board=board)
        self.assertEqual(code, 0, out)
        self.assertIn("replaces the board's copy", out)
        self.assertEqual(board.puts(), ['PUT mons/ps151.bin 20000'])

    def test_the_most_needed_on_the_way_must_fit_not_only_the_end(self):
        # A new file first, then one that shrinks: in the end they free space, but the first
        # needs its room before the second frees any.
        a, b = self.sprite('ps151.bin', 20000), self.sprite('ps025.bin', 100)
        old = {'ps025.bin': b'TPK2' + b'o' * 39996}
        net, peak = self.sd.plan([(a, 'ps151.bin', b'x' * 20000), (b, 'ps025.bin', b'x' * 100)],
                                 {n: len(d) for n, d in old.items()})
        self.assertLess(net, 0)
        board = FakeSdBoard(self, files=old, free=10000 + self.sd.FS_SPARE)
        code, _, _ = self.run_send(a, b, board=board)
        self.assertIsInstance(code, str)
        self.assertIn('not enough room', code)
        self.assertEqual(board.puts(), [])
        board = FakeSdBoard(self, files=old, free=10000 + self.sd.FS_SPARE)
        code, out, _ = self.run_send(b, a, board=board)   # the shrinking one first: it fits
        self.assertEqual(code, 0, out)
        self.assertEqual(len(board.puts()), 2)

    def test_check_and_ls_send_only_read_only_commands(self):
        board = FakeSdBoard(self, files={'p151.bin': b'TPK2' + b'n' * 9000}, free=900 * 1024)
        code, out, _ = self.run_send('--check', self.sprite('ps151.bin', 9000), board=board)
        self.assertEqual(code, 0)
        self.assertIn('They fit', out)
        self.assertIn('Nothing sent (--check).', out)
        self.assertEqual(board.commands, ['SDINFO', 'LS'])
        board = FakeSdBoard(self, files={'p151.bin': b'TPK2' + b'n' * 9000}, free=900 * 1024)
        code, out, _ = self.run_send('--ls', board=board)
        self.assertEqual(code, 0)
        self.assertEqual(board.commands, ['SDINFO', 'LS'])
        self.assertIn('p151.bin 9004', out)
        self.assertIn('900 KB free', out)

    def test_the_first_failure_stops_the_run_and_says_how_to_recover(self):
        board = FakeSdBoard(self, trouble={'mons/ps151.bin': ('err', 1)})
        a, b = self.sprite('ps151.bin', 9000), self.sprite('ps025.bin', 5000)
        code, out, _ = self.run_send(a, b, board=board)
        self.assertIsInstance(code, str, 'a failed file must exit non-zero')
        self.assertIn('ps151.bin: FAILED: it stopped after 2 KB of 9 KB (the board said ERR)', out)
        self.assertEqual(board.puts(), ['PUT mons/ps151.bin 9000'], 'went on after a failure')
        self.assertIn('Not sent: ps025.bin', code)
        self.assertIn('did not arrive whole', code)
        self.assertIn('"?"', code)
        self.assertIn(f'python3 tools/send_sd.py --port {board.port} {a} {b}', code)

    def test_a_refused_put_and_a_missing_done_are_failures(self):
        for what, why in (('refuse', 'the board refused it (ERR)'),
                          ('nodone', 'no DONE after the last block')):
            with self.subTest(what=what):
                board = FakeSdBoard(self, trouble={'mons/ps151.bin': (what, None)})
                code, out, _ = self.run_send(self.sprite('ps151.bin', 5000), board=board)
                self.assertIsInstance(code, str)
                self.assertIn(f'ps151.bin: FAILED: {why}', out)
                self.assertNotIn('sent,', out)

    def test_after_a_silent_board_it_waits_for_the_boards_err_before_letting_go(self):
        # The board still waits for the rest of the file: a command sent now would be taken as
        # data. It gives up after 5 s without data and says ERR; the script waits for that.
        FakeSdBoard.STALL, old = 1.5, FakeSdBoard.STALL
        self.addCleanup(setattr, FakeSdBoard, 'STALL', old)
        self.patch('ACK_TIMEOUT', 0.6)
        self.patch('RESYNC_TIMEOUT', 4)
        board = FakeSdBoard(self, trouble={'mons/ps151.bin': ('silent', 1)})
        code, out, _ = self.run_send(self.sprite('ps151.bin', 9000), board=board)
        ended = time.time()
        self.assertIsInstance(code, str)
        self.assertIn('it stopped after 2 KB of 9 KB (no answer)', out)
        self.assertIsNotNone(board.err_at, 'let go of the port before the board said ERR')
        self.assertLessEqual(board.err_at, ended)

    def test_heartbeats_between_the_answers_are_skipped(self):
        board = FakeSdBoard(self, chatter=True)
        code, out, _ = self.run_send(self.sprite('ps151.bin', 9000), board=board)
        self.assertEqual(code, 0, out)
        self.assertEqual(len(board.files['ps151.bin']), 9000)

    def test_no_storage_and_damaged_replies_send_nothing(self):
        a = self.sprite('ps151.bin', 900)
        n = b'TPK2' + b'n' * 9996
        for why, board, want in (
                ('no storage', FakeSdBoard(self, total=0), 'no sprite storage mounted'),
                ('used under the files', FakeSdBoard(self, files={'p001.bin': n}, other=-5000),
                 'disagree'),
                ('used over total', FakeSdBoard(self, files={'p001.bin': n}, total=4096),
                 'disagree')):
            with self.subTest(why):
                code, _, _ = self.run_send(a, board=board)
                self.assertIsInstance(code, str)
                self.assertIn(want, code)
                self.assertEqual(board.puts(), [])

    def test_parse_ls_takes_names_with_or_without_the_folder(self):
        self.assertEqual(self.sd.parse_ls(['/mons/ps151.bin 123', 'HEALTH up=1s', 'p001.bin 5',
                                           'cargado /mons/p001.bin (5 KB)']),
                         {'ps151.bin': 123, 'p001.bin': 5})

    def test_a_board_that_is_not_there_gives_a_message_not_a_traceback(self):
        r = subprocess.run([sys.executable, '-I', os.path.join(TOOLS, 'send_sd.py'),
                            '--port', '/dev/does-not-exist', 'ps151.bin'],
                           capture_output=True, text=True, timeout=30)
        self.assertNotEqual(r.returncode, 0)
        self.assertNotIn('Traceback', r.stderr)
        self.assertIn('send_sd.py:', r.stderr)


SIM = os.path.join(ROOT, 'sim')


def _load_sim_module(name):
    import importlib.util
    spec = importlib.util.spec_from_file_location('sim_' + name, os.path.join(SIM, name + '.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class TestSimGenSketch(unittest.TestCase):
    """sim/gen_sketch.py: los prototipos que arduino-cli genera para el .ino."""

    SAMPLE = (
        '#include <Arduino.h>\n'                                  # 1
        'struct Foo { int a; };\n'                                # 2
        'Foo foos[2] = {\n'                                       # 3
        '  {1}, {2},\n'                                           # 4
        '};\n'                                                    # 5
        'int first(int a, int b = 3) {\n'                         # 6
        '  if (a) {\n'                                            # 7
        '    return b;\n'                                         # 8
        '  }\n'                                                   # 9
        '  return later(a);\n'                                    # 10
        '}\n'                                                     # 11
        '#if BOARD_X\n'                                           # 12
        'static bool onlyX() {\n'                                 # 13
        '  return true;\n'                                        # 14
        '}\n'                                                     # 15
        '#else\n'                                                 # 16
        'inline void notX() { first(1); }\n'                      # 17
        '#endif\n'                                                # 18
        '/* void commented() {\n'                                 # 19
        '} */\n'                                                  # 20
        'const char *later(int v) {\n'                            # 21
        '  return "x";\n'                                         # 22
        '}\n'                                                     # 23
        'void IRAM_ATTR isr() {}\n'                               # 24
    )

    def setUp(self):
        self.gen = _load_sim_module('gen_sketch')
        self.lines = self.SAMPLE.splitlines(keepends=True)

    def test_encuentra_las_definiciones_de_nivel_superior(self):
        protos = [p for _, p, _ in self.gen.find_functions(self.lines)]
        self.assertEqual(protos, ['int first(int a, int b);', 'static bool onlyX();',
                                  'inline void notX();', 'const char *later(int v);',
                                  'void IRAM_ATTR isr();'])

    def test_los_prototipos_llevan_las_condiciones_de_su_definicion(self):
        funcs = {p: c for _, p, c in self.gen.find_functions(self.lines)}
        self.assertEqual(funcs['static bool onlyX();'], [['#if BOARD_X']])
        self.assertEqual(funcs['inline void notX();'], [['#if BOARD_X', '#else']])
        self.assertEqual(funcs['const char *later(int v);'], [])
        self.assertEqual(self.gen.wrap('inline void notX();', [['#if BOARD_X', '#else']]),
                         ['#if BOARD_X', '#else', 'inline void notX();', '#endif'])

    def test_los_prototipos_van_antes_de_la_primera_definicion_con_line(self):
        out = self.gen.generate('sample.ino', self.lines).splitlines()
        path = os.path.abspath('sample.ino').replace('\\', '/')
        self.assertEqual(out[0], '#include <Arduino.h>')
        self.assertEqual(out[1], '#line 1 "%s"' % path)
        self.assertEqual(out[2:7], [l.rstrip('\n') for l in self.lines[:5]])
        self.assertEqual(out[7], 'int first(int a, int b);')
        back = out.index('#line 6 "%s"' % path)
        self.assertEqual(out[back + 1], 'int first(int a, int b = 3) {')
        self.assertLess(out.index('const char *later(int v);'), back)
        self.assertNotIn('commented', '\n'.join(out[:back]))

    def test_cubre_todas_las_funciones_del_sketch_real(self):
        with open(os.path.join(ROOT, 'TamaPoke.ino'), encoding='utf-8', errors='surrogateescape') as f:
            lines = f.readlines()
        names = {re.match(r'.*?(\w+)\(', p).group(1) for _, p, _ in self.gen.find_functions(lines)}
        for must in ('setup', 'loop', 'render', 'handleTouch', 'wakeUI', 'flushUI', 'bedApply',
                     'attnLoop', 'readTrainingTouch', 'touchIsr', 'mixKey', 'onSwipe'):
            self.assertIn(must, names)
        # No name depends on where it sits in the file: the last definition of the .ino, found
        # with a plain column-0 pattern of its own (not the generator's regex), must be the last
        # function it returns, so a scan that stops early cannot pass, and neither can a stray
        # match after it. The pattern takes one-line definitions too ("int f() { return 1; }").
        last_def = re.compile(r'(?!(?:if|for|while|switch|else|do|return)\b)[A-Za-z_][\w \t*&:<>,]*\w\([^;{}]*\)\s*(?:const\s*)?\{(?:.*\})?\s*$')
        defs = [i for i, ln in enumerate(lines) if last_def.match(ln)]
        self.assertTrue(defs)
        found = max(i for i, _, _ in self.gen.find_functions(lines))
        self.assertEqual(found, defs[-1], 'the last definition find_functions returns is line %d %r, the '
                         "file's last is line %d %r" % (found + 1, lines[found], defs[-1] + 1, lines[defs[-1]]))
        out = self.gen.generate(os.path.join(ROOT, 'TamaPoke.ino'), lines)
        self.assertEqual(out.count('\n#line '), 2)


class TestSimPng(unittest.TestCase):
    """sim/png.cpp: la imagen que escribe test_sim.cpp, leida con zlib de Python."""

    def _read(self, name):
        import struct
        import zlib
        path = os.path.join(ROOT, 'test', 'build', name)
        if not os.path.exists(path):
            if os.environ.get('TAMAPOKE_RUN_TESTS'):
                # run_tests.sh deletes the file, then runs the C++ suite that writes it
                self.fail('%s was not written by the C++ suite (test_sim.cpp)' % name)
            self.skipTest('run the C++ tests first (test/run_tests.sh writes %s)' % name)
        with open(path, 'rb') as f:
            data = f.read()
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        pos, idat = 8, b''
        w = h = None
        while pos < len(data):
            n, kind = struct.unpack('>I4s', data[pos:pos + 8])
            body = data[pos + 8:pos + 8 + n]
            crc, = struct.unpack('>I', data[pos + 8 + n:pos + 12 + n])
            self.assertEqual(crc, zlib.crc32(kind + body) & 0xFFFFFFFF, kind)
            if kind == b'IHDR':
                w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
                self.assertEqual((depth, ctype), (8, 2))
            elif kind == b'IDAT':
                idat += body
            pos += 12 + n
        raw = zlib.decompress(idat)
        stride = w * 3
        self.assertEqual(len(raw), h * (stride + 1))
        rows, prev, self.filters = [], bytearray(stride), set()
        for y in range(h):
            ftype = raw[y * (stride + 1)]
            self.filters.add(ftype)
            line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
            for i in range(stride):
                if ftype == 1:
                    line[i] = (line[i] + (line[i - 3] if i >= 3 else 0)) & 0xFF
                elif ftype == 2:
                    line[i] = (line[i] + prev[i]) & 0xFF
                else:
                    self.assertEqual(ftype, 0)
            rows.append(line)
            prev = line
        return w, h, rows

    @staticmethod
    def _rgb(c):
        return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)

    @staticmethod
    def _pixel(x, y):  # el mismo patron que selftestPixel() en test_sim.cpp
        return 0x1234 if x < 20 else (x * 2017 + y * 31 + x * y) & 0xFFFF

    def test_zlib_de_python_lee_la_imagen_pixel_a_pixel(self):
        w, h, rows = self._read('sim_png_selftest.png')
        self.assertEqual((w, h), (37, 23))
        self.assertEqual(self.filters, {1, 2})  # filas Sub y filas Up: las dos reconstrucciones a prueba
        for y in range(h):
            for x in range(w):
                self.assertEqual(tuple(rows[y][x * 3:x * 3 + 3]), self._rgb(self._pixel(x, y)), (x, y))

    def test_la_mascara_redonda_deja_negras_las_esquinas(self):
        w, h, rows = self._read('sim_png_selftest_round.png')
        self.assertEqual(tuple(rows[0][0:3]), (0, 0, 0))
        self.assertEqual(tuple(rows[h - 1][(w - 1) * 3:w * 3]), (0, 0, 0))
        cx, cy = w // 2, h // 2
        self.assertEqual(tuple(rows[cy][cx * 3:cx * 3 + 3]), self._rgb(self._pixel(cx, cy)))


if __name__ == '__main__':
    unittest.main(verbosity=2)
