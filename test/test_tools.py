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
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, 'tools')
sys.path.insert(0, TOOLS)


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
        from dex_names import LOCAL_NAMES
        cls.DEX, cls.ACCENTS, cls.CLASSIC = DEX, TYPE_ACCENTS, CLASSIC
        cls.RARE, cls.LEGENDARY, cls.SLUGS = RARE, LEGENDARY, SLUGS
        cls.STATS, cls.NAMES = BASE_STATS, LOCAL_NAMES
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

    def test_los_nombres_traducidos_cuadran_con_su_fuente(self):
        # Los latinos se pintan con la fuente CP437 (un byte por caracter, sin
        # minusculas en los nombres); el japones y el coreano con una fuente
        # U8g2 y van en UTF-8, asi que ni son ASCII ni tienen mayusculas que
        # comprobar.
        LATINOS, CJK = ('fr', 'de'), ('ja', 'ko')
        for num, langs in self.NAMES.items():
            self.assertIn(num, self.byNum, f'nombre traducido de una especie inexistente: {num}')
            for lang, name in langs.items():
                self.assertIn(lang, LATINOS + CJK, f'idioma inesperado: {lang}')
                self.assertTrue(name, f'{num} {lang} vacio')
                if lang in LATINOS:
                    self.assertTrue(name.isascii(), f'{num} {lang} no es ASCII: {name}')
                    self.assertEqual(name, name.upper(), f'{num} {lang} lleva minusculas: {name}')
                    self.assertLessEqual(len(name), 12, f'{num} {lang} no cabe en pantalla: {name}')
                else:
                    # el limite util son CARACTERES, no bytes: en UTF-8 cada kana o silaba hangul ocupa 3
                    self.assertLessEqual(len(name), 8, f'{num} {lang} no cabe en pantalla: {name}')


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
    """Comprobaciones baratas sobre el .ino que no necesitan compilarlo."""

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(ROOT, 'TamaPoke.ino')) as fh:
            cls.ino = fh.read()

    def test_el_sketch_no_lleva_caracteres_no_ascii_en_literales_de_pantalla(self):
        # los comentarios pueden llevar lo que quieran; los literales, no:
        # la fuente GFX es ASCII y un acento sale como basura
        for lineno, line in enumerate(self.ino.splitlines(), 1):
            code = line.split('//')[0]
            for chunk in code.split('"')[1::2]:
                self.assertTrue(chunk.isascii(),
                                f'TamaPoke.ino:{lineno}: literal no ASCII: {chunk!r}')

    def test_las_llaves_estan_equilibradas(self):
        self.assertEqual(self.ino.count('{'), self.ino.count('}'),
                         'llaves descompensadas en TamaPoke.ino')



class TestBuffersDeTexto(unittest.TestCase):
    """Cada snprintf(buf, sizeof(buf), T(S_x), ...) cabe en su buffer en TODOS los idiomas.

    Los buffers se dimensionaron cuando toda cadena era de un byte por
    caracter. En UTF-8 un kana o una silaba hangul ocupan 3, y snprintf corta
    en silencio: a veces se pierden cifras ("きろく 2" por "きろく 219") y a
    veces corta a mitad de un caracter, que la fuente ya no sabe dibujar. No lo
    avisa ni el compilador ni la pantalla, asi que se comprueba aqui con el
    peor caso de cada argumento segun su tipo:

      %u / %d  -> 5 cifras (todos los argumentos son de 16 bits o menos)
      %lu      -> 10 cifras
      %s       -> lo que declare ARGS_S; un %s sin declarar hace fallar el
                  test, para que quien lo anada piense cuanto puede ocupar
    """

    ARGS_S = {  # que puede llegar por cada %s, en orden
        'S_NAME_FMT': ('estrella', 'nombre_o_apodo'),
        'S_RELEASE_FMT': ('nombre',),
        'S_INFO_FMT': ('baya',),
        'S_FAREWELL_BTN': ('nombre_o_apodo',),
        'S_RUNAWAY_BTN': ('nombre_o_apodo',),
    }
    CIFRAS = {'u': 5, 'd': 5, 'lu': 10}
    ESPEC = re.compile(r'%0?\d*(lu|u|d|s)')

    @classmethod
    def setUpClass(cls):
        import test_i18n_formats as fmt
        from dex_data import DEX
        from dex_names import LOCAL_NAMES

        def leer(nombre):
            with open(os.path.join(ROOT, nombre), encoding='utf-8') as fh:
                return fh.read()

        cls.LANGS = fmt.LANGS
        cuerpo = re.search(r'enum\s+StrId[^{]*\{(.*?)\}', leer('i18n.h'), re.S).group(1)
        ids = re.findall(r'\b(S_[A-Z0-9_]+)\b', cuerpo)
        filas = fmt.extract_table(leer('i18n.cpp'), 'STRINGS', len(cls.LANGS))
        cls.LIT = {sid: [fila[k] for fila in filas] for k, sid in enumerate(ids)}
        cls.ino = leer('TamaPoke.ino').splitlines()
        cls.APODO = int(re.search(r'char\s+nick\[(\d+)\]', leer('pet.h')).group(1)) - 1
        base = max(len(row[2].encode()) for row in DEX)
        cls.NOMBRE = {}
        for lang in cls.LANGS:
            propios = [n[lang.lower()] for n in LOCAL_NAMES.values() if lang.lower() in n]
            cls.NOMBRE[lang] = max([base] + [len(n.encode('utf-8')) for n in propios])

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
            elif arg == 'baya':
                total += max(self.bytes_literal(self.LIT[b][li]) for b in
                             ('S_BERRY_UNK', 'S_BERRY_RED', 'S_BERRY_BLUE', 'S_BERRY_GREEN'))
        return total

    def test_cada_texto_formateado_cabe_en_su_buffer_en_todos_los_idiomas(self):
        llamada = re.compile(r'snprintf\((\w+),\s*sizeof\(\1\),\s*T\((S_[A-Z0-9_]+)\)')
        revisadas = 0
        for n, linea in enumerate(self.ino, 1):
            m = llamada.search(linea.split('//')[0])
            if not m:
                continue
            var, sid = m.groups()
            tam = next((int(d.group(1)) for k in range(n - 2, -1, -1)
                        for d in [re.search(r'\bchar\s+' + var + r'\s*\[(\d+)\]', self.ino[k])]
                        if d), None)
            self.assertIsNotNone(tam, f'TamaPoke.ino:{n}: no encuentro la declaracion de {var}')
            revisadas += 1
            for li, lang in enumerate(self.LANGS):
                with self.subTest(linea=n, id=sid, idioma=lang):
                    peor = self.peor_caso(sid, li)
                    self.assertLessEqual(
                        peor, tam,
                        f'TamaPoke.ino:{n}: {var}[{tam}] con {sid} en {lang} necesita {peor} bytes')
        self.assertGreater(revisadas, 10, 'el patron de snprintf ya no encuentra las llamadas')


class TestBoardScripts(unittest.TestCase):
    """flash_175c.sh / pet_save.sh / board_env.sh against a fake esptool that
    only records its arguments, so nothing here touches a real board."""

    NVS_BYTES = 0x5000

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.log = os.path.join(self.tmp, 'esptool.log')
        self.fake = os.path.join(self.tmp, 'esptool')
        # Records each call; for read-flash it writes OFFSET-sized zeros to the
        # destination, like the real tool would.
        with open(self.fake, 'w') as fh:
            fh.write('#!/usr/bin/env bash\n'
                     f'echo "$*" >> "{self.log}"\n'
                     'args=("$@")\n'
                     'for i in "${!args[@]}"; do\n'
                     '  if [[ "${args[$i]}" == read-flash ]]; then\n'
                     '    size=$(( ${args[$((i+2))]} )); dest="${args[$((i+3))]}"\n'
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
        open(os.path.join(outdir, 'TamaPoke.ino.bin'), 'wb').close()
        r = self.run_tool('flash_175c.sh', '--port', self.port, '--out', outdir, '--app-only')
        self.assertEqual(r.returncode, 0, r.stderr)
        (call,) = self.calls()
        addrs = [a for a in call if a.startswith('0x')]
        self.assertEqual(addrs, ['0x10000'], f'app-only must not write anything else: {call}')

    def test_restore_refuses_a_file_that_is_not_a_save(self):
        bad = os.path.join(self.tmp, 'not-a-save.bin')
        with open(bad, 'wb') as fh:
            fh.write(b'\0' * (self.NVS_BYTES - 1))
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', bad)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('Refusing', r.stderr)
        self.assertFalse(any('write-flash' in c for c in self.calls()), 'wrote a bad save')

    def test_restore_refuses_a_save_that_fails_its_checksum(self):
        save = os.path.join(self.tmp, 'pet.nvs.bin')
        with open(save, 'wb') as fh:
            fh.write(b'\0' * self.NVS_BYTES)
        with open(save + '.sha256', 'w') as fh:
            fh.write('0' * 64 + '  pet.nvs.bin\n')
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse(any('write-flash' in c for c in self.calls()), 'wrote a corrupted save')

    def test_restore_backs_up_the_current_pet_before_writing_nvs_only(self):
        save = os.path.join(self.tmp, 'pet.nvs.bin')
        with open(save, 'wb') as fh:
            fh.write(b'\0' * self.NVS_BYTES)
        r = self.run_tool('pet_save.sh', 'restore', '--port', self.port, '--file', save)
        self.assertEqual(r.returncode, 0, r.stderr)
        verbs = [next(a for a in c if a in ('read-flash', 'write-flash')) for c in self.calls()]
        self.assertEqual(verbs, ['read-flash', 'write-flash'], 'must back up before writing')
        write = self.calls()[1]
        self.assertEqual([a for a in write if a.startswith('0x')], ['0x9000'])
        backups = os.listdir(os.path.join(self.tmp, 'backups'))
        self.assertTrue(any(b.startswith('pre-restore-') and b.endswith('.nvs.bin') for b in backups))

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


class TestBoardPy(unittest.TestCase):
    def setUp(self):
        import board
        self.board = board

    def test_wallclock_epoch_is_local_time_written_as_utc(self):
        # The firmware reads the epoch back with gmtime_r and shows it as the
        # local time, so the fields must match localtime, not the true epoch.
        now = 1_794_562_514
        ep = self.board.local_wallclock_epoch(now)
        self.assertEqual(time.gmtime(ep)[:6], time.localtime(now)[:6])

    def test_parse_rtc(self):
        self.assertEqual(self.board.parse_rtc(['x', 'rtc=1794562514']), 1794562514)
        self.assertIsNone(self.board.parse_rtc(['spec=4 rtc=5']))
        self.assertIsNone(self.board.parse_rtc(['rtc=abc']))


if __name__ == '__main__':
    unittest.main(verbosity=2)
