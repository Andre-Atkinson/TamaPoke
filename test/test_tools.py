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
# TamaPoke.ino.bin in --output-dir and, when FAKE_PARTS is set, that partition
# table in --build-path (what the core's prebuild hook would have copied).
FAKE_ARDUINO_CLI = r'''#!/usr/bin/env python3
import json, os, shutil, sys
args = sys.argv[1:]
with open(os.environ['FAKE_CLI_LOG'], 'a') as fh:
    fh.write(json.dumps(args) + '\n')
def opt(name):
    return args[args.index(name) + 1] if name in args else None
if opt('--output-dir'):
    os.makedirs(opt('--output-dir'), exist_ok=True)
    with open(os.path.join(opt('--output-dir'), 'TamaPoke.ino.bin'), 'wb') as fh:
        fh.write(b'\0' * 1024)
if opt('--build-path') and os.environ.get('FAKE_PARTS'):
    os.makedirs(opt('--build-path'), exist_ok=True)
    shutil.copy(os.environ['FAKE_PARTS'], os.path.join(opt('--build-path'), 'partitions.csv'))
'''


class TestBuildJobs(unittest.TestCase):
    """build_175c.sh and the gate's original-1.75 build cap arduino-cli's
    parallel compile jobs (its own default is every core), checked against a
    fake arduino-cli on PATH, so nothing here really compiles."""

    PARTS = os.path.join(ROOT, 'boards', '175c', 'partitions.csv')
    # Both scripts carry their own copy of the rule; these pin both, from either
    # side. '08' is octal to arduino-cli (and invalid), '010' would be 8 jobs.
    GOOD_JOBS = ('1', '0', '8', '9999')
    BAD_JOBS = ('x', '08', '010', '-1', '3 ', '2.5', '10000')

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.log = os.path.join(self.tmp, 'arduino-cli.log')
        bindir = os.path.join(self.tmp, 'bin')
        os.makedirs(bindir)
        fake = os.path.join(bindir, 'arduino-cli')
        with open(fake, 'w') as fh:
            fh.write(FAKE_ARDUINO_CLI)
        os.chmod(fake, 0o755)
        # An owner's own TAMAPOKE_JOBS (or board overrides) must not leak in.
        self.env = {k: v for k, v in os.environ.items()
                    if k not in ('TAMAPOKE_JOBS', 'TAMAPOKE_OUT', 'FQBN_175C', 'APP_MAX_OVERRIDE')}
        self.env.update(PATH=bindir + os.pathsep + os.environ.get('PATH', ''),
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

    def build_175c(self, **env):
        return subprocess.run([os.path.join(TOOLS, 'build_175c.sh'), '--out', os.path.join(self.tmp, 'out')],
                              env=dict(self.env, **env), capture_output=True, text=True, timeout=60)

    def gate_root(self):
        """A minimal repo for gate.sh: the real gate and 1.75C build script,
        the 1.75C table, a one-line sketch, and stand-ins for the host tests,
        the i18n check and the desktop simulator that record that they ran."""
        root = os.path.join(self.tmp, 'repo')
        os.makedirs(os.path.join(root, 'tools'))
        os.makedirs(os.path.join(root, 'test'))
        os.makedirs(os.path.join(root, 'boards', '175c'))
        os.makedirs(os.path.join(root, 'sim', 'scripts'))
        put(os.path.join(root, 'sim', 'scripts', 'screens.txt'))   # no `shot` lines: 0/0 screens
        for name in ('gate.sh', 'build_175c.sh'):
            shutil.copy2(os.path.join(TOOLS, name), os.path.join(root, 'tools', name))
        shutil.copy2(self.PARTS, os.path.join(root, 'boards', '175c', 'partitions.csv'))
        put(os.path.join(root, 'TamaPoke.ino'), b'void setup() {}\nvoid loop() {}\n')
        self.ran = os.path.join(self.tmp, 'ran')
        stub = os.path.join(root, 'test', 'run_tests.sh')
        put(stub, f'#!/usr/bin/env bash\necho tests >> "{self.ran}"\necho "TODO OK"\n'.encode())
        os.chmod(stub, 0o755)
        put(os.path.join(root, 'tools', 'test_i18n_formats.py'),
            f'open({self.ran!r}, "a").write("i18n\\n")\n'.encode())
        sim = os.path.join(root, 'tools', 'sim.sh')
        put(sim, f'#!/usr/bin/env bash\necho sim >> "{self.ran}"\n'.encode())
        os.chmod(sim, 0o755)
        return root

    def gate(self, root, *args, **env):
        return subprocess.run([os.path.join(root, 'tools', 'gate.sh'), *args],
                              env=dict(self.env, **env), capture_output=True, text=True, timeout=120)

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

    def test_an_unplugged_board_gives_a_message_not_a_traceback(self):
        for port in ('/dev/does-not-exist', __file__):   # missing, and not a tty
            with self.subTest(port=port):
                r = subprocess.run([sys.executable, '-I', os.path.join(TOOLS, 'board.py'),
                                    '--port', port, 'status'],
                                   capture_output=True, text=True, timeout=30)
                self.assertNotEqual(r.returncode, 0)
                self.assertNotIn('Traceback', r.stderr)
                self.assertIn('board.py:', r.stderr)


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
        for must in ('setup', 'loop', 'render', 'handleTouch', 'wakeUI', 'flushUI', 'renderCardCare',
                     'careTap', 'readTrainingTouch', 'touchIsr', 'printT', 'drawMap'):
            self.assertIn(must, names)
        out = self.gen.generate(os.path.join(ROOT, 'TamaPoke.ino'), lines)
        self.assertEqual(out.count('\n#line '), 2)


class TestSimExtractFonts(unittest.TestCase):
    """sim/extract_fonts.py: copia arrays de u8g2_fonts.c sin compilar el fichero entero."""

    SRC = (b'#include "u8g2.h"\n'
           b'const uint8_t u8g2_font_ab[1] U8G2_FONT_SECTION("u8g2_font_ab") = "z";\n'
           b'const uint8_t u8g2_font_a[5] U8G2_FONT_SECTION("u8g2_font_a") = \n'
           b'  "ab;c"\n'
           b'  "\\42;\\0";\n'
           b'const uint8_t u8g2_font_b[2] U8G2_FONT_SECTION("u8g2_font_b") = \n'
           b'  "\\377\\1";\n')

    def setUp(self):
        self.mod = _load_sim_module('extract_fonts')

    def test_saca_los_arrays_pedidos_enteros_y_sin_secciones(self):
        out = self.mod.extract(self.SRC, ['u8g2_font_b', 'u8g2_font_a']).decode()
        self.assertIn('const uint8_t u8g2_font_a[5] = \n  "ab;c"\n  "\\42;\\0";', out)  # ';' dentro de la cadena
        self.assertIn('const uint8_t u8g2_font_b[2] = \n  "\\377\\1";', out)
        self.assertNotIn('U8G2_FONT_SECTION', out)
        self.assertNotIn('u8g2_font_ab', out)  # nombre parecido (y antes en el fichero): no se cuela

    def test_un_nombre_que_no_existe_falla(self):
        with self.assertRaises(SystemExit):
            self.mod.extract(self.SRC, ['u8g2_font_missing'])


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
