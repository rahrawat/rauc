#include <stdio.h>
#include <locale.h>
#include <glib.h>

#include <bootchooser.h>
#include <context.h>
#include <utils.h>

#include "common.h"

typedef struct {
	gchar *tmpdir;
} BootchooserFixture;

static void bootchooser_fixture_set_up(BootchooserFixture *fixture,
		gconstpointer user_data)
{
	fixture->tmpdir = g_dir_make_tmp("rauc-bootchooser-XXXXXX", NULL);
	g_assert_nonnull(fixture->tmpdir);
}

static void bootchooser_fixture_tear_down(BootchooserFixture *fixture,
		gconstpointer user_data)
{
	g_assert_true(rm_tree(fixture->tmpdir, NULL));
	g_free(fixture->tmpdir);
}

static void bootchooser_barebox(BootchooserFixture *fixture,
		gconstpointer user_data)
{
	RaucSlot *rootfs0 = NULL, *rootfs1 = NULL, *primary = NULL;
	gboolean good;

	const gchar *cfg_file = "\
[system]\n\
compatible=FooCorp Super BarBazzer\n\
bootloader=barebox\n\
mountprefix=/mnt/myrauc/\n\
\n\
[keyring]\n\
path=/etc/rauc/keyring/\n\
\n\
[slot.recovery.0]\n\
device=/dev/recovery-0\n\
type=raw\n\
readonly=true\n\
\n\
[slot.rootfs.0]\n\
device=/dev/rootfs-0\n\
type=ext4\n\
bootname=system0\n\
\n\
[slot.rootfs.1]\n\
device=/dev/rootfs-1\n\
type=ext4\n\
bootname=system1\n";

	gchar* pathname = write_tmp_file(fixture->tmpdir, "barebox.conf", cfg_file, NULL);
	g_assert_nonnull(pathname);

	g_clear_pointer(&r_context_conf()->configpath, g_free);
	r_context_conf()->configpath = pathname;
	r_context();

	rootfs0 = find_config_slot_by_device(r_context()->config, "/dev/rootfs-0");
	g_assert_nonnull(rootfs0);
	rootfs1 = find_config_slot_by_device(r_context()->config, "/dev/rootfs-1");
	g_assert_nonnull(rootfs1);

	/* check rootfs.0 and rootfs.1 are considered good */
	g_setenv("BAREBOX_STATE_VARS_PRE", " \
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=20\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=10\n\
", TRUE);
	g_assert_true(r_boot_get_state(rootfs0, &good, NULL));
	g_assert_true(good);
	g_assert_true(r_boot_get_state(rootfs1, &good, NULL));
	g_assert_true(good);
	/* check rootfs.0 is considered as primary */
	primary = r_boot_get_primary(NULL);
	g_assert_nonnull(primary);
	g_assert(primary == rootfs0);
	g_assert(primary != rootfs1);

	/* check rootfs.0 is considered bad (remaining_attempts = 0) */
	g_setenv("BAREBOX_STATE_VARS_PRE", " \
bootstate.system0.remaining_attempts=0\n\
bootstate.system0.priority=20\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=10\n\
", TRUE);
	g_assert_true(r_boot_get_state(rootfs0, &good, NULL));
	g_assert_false(good);
	g_assert_true(r_boot_get_state(rootfs1, &good, NULL));
	g_assert_true(good);
	/* check rootfs.1 is considered as primary */
	primary = r_boot_get_primary(NULL);
	g_assert_nonnull(primary);
	g_assert(primary != rootfs0);
	g_assert(primary == rootfs1);

	/* check rootfs.0 is considered bad (priority = 0) */
	g_setenv("BAREBOX_STATE_VARS_PRE", " \
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=0\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=10\n\
", TRUE);
	g_assert_true(r_boot_get_state(rootfs0, &good, NULL));
	g_assert_false(good);
	g_assert_true(r_boot_get_state(rootfs1, &good, NULL));
	g_assert_true(good);
	/* check rootfs.1 is considered as primary */
	primary = r_boot_get_primary(NULL);
	g_assert_nonnull(primary);
	g_assert(primary != rootfs0);
	g_assert(primary == rootfs1);

	/* check rootfs.0 is marked good (has remaining attempts reset 1->3) */
	g_setenv("BAREBOX_STATE_VARS_PRE", " \
bootstate.system0.remaining_attempts=1\n\
bootstate.system0.priority=20\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=10\n\
", TRUE);
	g_setenv("BAREBOX_STATE_VARS_POST", " \
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=20\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=10\n\
", TRUE);
	g_assert_true(r_boot_set_state(rootfs0, TRUE, NULL));

	/* check rootfs.0 is marked bad (prio and attempts 0) */
	g_setenv("BAREBOX_STATE_VARS_PRE", " \
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=20\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=10\n\
", TRUE);
	g_setenv("BAREBOX_STATE_VARS_POST", " \
bootstate.system0.remaining_attempts=0\n\
bootstate.system0.priority=0\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=10\n\
", TRUE);
	g_assert_true(r_boot_set_state(rootfs0, FALSE, NULL));

	/* check rootfs.1 is marked primary (prio set to 20, others to 10) */
	g_setenv("BAREBOX_STATE_VARS_PRE", " \
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=20\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=10\n\
", TRUE);
	g_setenv("BAREBOX_STATE_VARS_POST", " \
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=10\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=20\n\
", TRUE);
	g_assert_true(r_boot_set_primary(rootfs1, NULL));

	/* check rootfs.1 is marked primary while current remains disabled (prio set to 20, others to 10) */
	g_setenv("BAREBOX_STATE_VARS_PRE", " \
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=0\n\
bootstate.system1.remaining_attempts=0\n\
bootstate.system1.priority=10\n\
", TRUE);
	g_setenv("BAREBOX_STATE_VARS_POST", " \
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=0\n\
bootstate.system1.remaining_attempts=3\n\
bootstate.system1.priority=20\n\
", TRUE);
	g_assert_true(r_boot_set_primary(rootfs1, NULL));
}

static void bootchooser_barebox_asymmetric(BootchooserFixture *fixture,
		gconstpointer user_data)
{
	GError *ierror = NULL;
	gboolean res = FALSE;
	RaucSlot *recovery = NULL, *rootfs0 = NULL, *primary = NULL;

	const gchar *cfg_file = "\
[system]\n\
compatible=FooCorp Super BarBazzer\n\
bootloader=barebox\n\
mountprefix=/mnt/myrauc/\n\
\n\
[keyring]\n\
path=/etc/rauc/keyring/\n\
\n\
[slot.recovery.0]\n\
device=/dev/recovery-0\n\
type=raw\n\
bootname=recovery\n\
readonly=true\n\
\n\
[slot.rootfs.0]\n\
device=/dev/rootfs-0\n\
type=ext4\n\
bootname=system0\n";

	gchar* pathname = write_tmp_file(fixture->tmpdir, "barebox_asymmetric.conf", cfg_file, NULL);
	g_assert_nonnull(pathname);

	g_clear_pointer(&r_context_conf()->configpath, g_free);
	r_context_conf()->configpath = pathname;
	r_context();

	recovery = find_config_slot_by_device(r_context()->config, "/dev/recovery-0");
	g_assert_nonnull(recovery);
	rootfs0 = find_config_slot_by_device(r_context()->config, "/dev/rootfs-0");
	g_assert_nonnull(rootfs0);


	/* check rootfs.0 is marked bad (prio and attempts 0) for asymmetric update scenarios */
	g_setenv("BAREBOX_STATE_VARS_PRE", " \
bootstate.recovery.remaining_attempts=3\n\
bootstate.recovery.priority=20\n\
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=10\n\
", TRUE);
	g_setenv("BAREBOX_STATE_VARS_POST", " \
bootstate.recovery.remaining_attempts=3\n\
bootstate.recovery.priority=20\n\
bootstate.system0.remaining_attempts=0\n\
bootstate.system0.priority=0\n\
", TRUE);
	res = r_boot_set_state(rootfs0, FALSE, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	/* check rootfs.0 is marked primary for asymmetric update scenarios */
	g_setenv("BAREBOX_STATE_VARS_PRE", " \
bootstate.recovery.remaining_attempts=3\n\
bootstate.recovery.priority=20\n\
bootstate.system0.remaining_attempts=0\n\
bootstate.system0.priority=0\n\
", TRUE);
	g_setenv("BAREBOX_STATE_VARS_POST", " \
bootstate.recovery.remaining_attempts=3\n\
bootstate.recovery.priority=10\n\
bootstate.system0.remaining_attempts=3\n\
bootstate.system0.priority=20\n\
", TRUE);
	res = r_boot_set_primary(rootfs0, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	primary = r_boot_get_primary(NULL);
	g_assert_nonnull(primary);
	g_assert(primary != rootfs0);
	g_assert(primary == recovery);
}

static void bootchooser_grub(BootchooserFixture *fixture,
		gconstpointer user_data)
{
	GError *ierror = NULL;
	gboolean res = FALSE;
	RaucSlot *slot;

	const gchar *cfg_file = "\
[system]\n\
compatible=FooCorp Super BarBazzer\n\
bootloader=grub\n\
grubenv=grubenv.test\n\
mountprefix=/mnt/myrauc/\n\
\n\
[keyring]\n\
path=/etc/rauc/keyring/\n\
\n\
[slot.rescue.0]\n\
device=/dev/rescue-0\n\
type=raw\n\
readonly=true\n\
\n\
[slot.rootfs.0]\n\
device=/dev/rootfs-0\n\
type=ext4\n\
bootname=A\n\
\n\
[slot.rootfs.1]\n\
device=/dev/rootfs-1\n\
type=ext4\n\
bootname=B\n";

	gchar* pathname = write_tmp_file(fixture->tmpdir, "grub.conf", cfg_file, NULL);
	g_assert_nonnull(pathname);

	g_clear_pointer(&r_context_conf()->configpath, g_free);
	r_context_conf()->configpath = pathname;
	r_context();

	slot = find_config_slot_by_device(r_context()->config, "/dev/rootfs-0");
	g_assert_nonnull(slot);

	res = r_boot_set_state(slot, TRUE, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);
	res = r_boot_set_state(slot, FALSE, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);

	slot = find_config_slot_by_device(r_context()->config, "/dev/rootfs-1");
	g_assert_nonnull(slot);

	res = r_boot_set_primary(slot, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);
}

/* Write content to state storage for uboot fw_setenv / fw_printenv RAUC mock
 * tools. Content should be similar to:
 * "\
 * BOOT_ORDER=A B\n\
 * BOOT_A_LEFT=3\n\
 * BOOT_B_LEFT=3\n\
 * "
 */
static void test_uboot_initialize_state(const gchar *vars)
{
	g_autofree gchar *state_path = g_build_filename(g_get_tmp_dir(), "uboot-test-state", NULL);
	g_setenv("UBOOT_STATE_PATH", state_path, TRUE);
	g_assert_true(g_file_set_contents(state_path, vars, -1, NULL));
}

/* Write desired target content of variables set by RAUC's fw_setenv /
 * fw_printenv mock tools for asserting correct behavior.
 * Content should identical to format described for
 * test_uboot_initialize_state().
 *
 * Returns TRUE if mock tools state content equals desired content,
 * FALSE otherwise
 */
static gboolean test_uboot_post_state(const gchar *compare)
{
	g_autofree gchar *state_path = g_build_filename(g_get_tmp_dir(), "uboot-test-state", NULL);
	g_autofree gchar *contents = NULL;

	g_assert_true(g_file_get_contents(state_path, &contents, NULL, NULL));

	if (g_strcmp0(contents, compare) != 0) {
		g_print("Error: '%s' and '%s' differ\n", contents, compare);
		return FALSE;
	}

	return TRUE;
}

static void bootchooser_uboot(BootchooserFixture *fixture,
		gconstpointer user_data)
{
	RaucSlot *rootfs0 = NULL;
	RaucSlot *rootfs1 = NULL;
	RaucSlot *primary = NULL;
	gboolean good;
	GError *error = NULL;

	const gchar *cfg_file = "\
[system]\n\
compatible=FooCorp Super BarBazzer\n\
bootloader=uboot\n\
mountprefix=/mnt/myrauc/\n\
\n\
[keyring]\n\
path=/etc/rauc/keyring/\n\
\n\
[slot.rescue.0]\n\
device=/dev/mtd4\n\
type=raw\n\
bootname=R\n\
readonly=true\n\
\n\
[slot.rootfs.0]\n\
device=/dev/rootfs-0\n\
type=ext4\n\
bootname=A\n\
\n\
[slot.rootfs.1]\n\
device=/dev/rootfs-1\n\
type=ext4\n\
bootname=B\n";

	gchar* pathname = write_tmp_file(fixture->tmpdir, "uboot.conf", cfg_file, NULL);
	g_assert_nonnull(pathname);

	g_clear_pointer(&r_context_conf()->configpath, g_free);
	r_context_conf()->configpath = pathname;
	r_context();

	rootfs0 = find_config_slot_by_device(r_context()->config, "/dev/rootfs-0");
	g_assert_nonnull(rootfs0);
	rootfs1 = find_config_slot_by_device(r_context()->config, "/dev/rootfs-1");
	g_assert_nonnull(rootfs1);

	/* check rootfs.0 and rootfs.1 are considered bad (as not in BOOT_ORDER / no attempts left) */
	test_uboot_initialize_state("\
BOOT_ORDER=B\n\
BOOT_A_LEFT=3\n\
BOOT_B_LEFT=0\n\
");
	g_assert_true(r_boot_get_state(rootfs0, &good, NULL));
	g_assert_false(good);
	g_assert_true(r_boot_get_state(rootfs1, &good, NULL));
	g_assert_false(good);

	/* check rootfs.1 is considered primary (as rootfs.0 has BOOT_A_LEFT set to 0) */
	test_uboot_initialize_state("\
BOOT_ORDER=A B\n\
BOOT_A_LEFT=0\n\
BOOT_B_LEFT=3\n\
");
	primary = r_boot_get_primary(NULL);
	g_assert_nonnull(primary);
	g_assert(primary != rootfs0);
	g_assert(primary == rootfs1);

	/* check none is considered primary (as rootfs.0 has BOOT_A_LEFT set to 0 and rootfs.1 is not in BOOT_ORDER) */
	test_uboot_initialize_state("\
BOOT_ORDER=A\n\
BOOT_A_LEFT=0\n\
BOOT_B_LEFT=3\n\
");
	primary = r_boot_get_primary(&error);
	g_assert_null(primary);
	g_assert_error(error, R_BOOTCHOOSER_ERROR, R_BOOTCHOOSER_ERROR_PARSE_FAILED);
	g_clear_error(&error);

	/* check rootfs.0 + rootfs.1 are considered good */
	test_uboot_initialize_state("\
BOOT_ORDER=A B\n\
BOOT_A_LEFT=3\n\
BOOT_B_LEFT=3\n\
");
	g_assert_true(r_boot_get_state(rootfs0, &good, NULL));
	g_assert_true(good);
	g_assert_true(r_boot_get_state(rootfs1, &good, NULL));
	g_assert_true(good);

	/* check rootfs.0 is marked bad (BOOT_A_LEFT set to 0) */
	g_assert_true(r_boot_set_state(rootfs0, FALSE, NULL));
	g_assert_true(test_uboot_post_state("\
BOOT_ORDER=B\n\
BOOT_A_LEFT=0\n\
BOOT_B_LEFT=3\n\
"));
	/* check rootfs.0 is considered bad*/
	g_assert_true(r_boot_get_state(rootfs0, &good, NULL));
	g_assert_false(good);

	/* check rootfs.0 is marked good again (BOOT_A_LEFT reset to 3) */
	g_assert_true(r_boot_set_state(rootfs0, TRUE, NULL));
	g_assert_true(test_uboot_post_state("\
BOOT_ORDER=B\n\
BOOT_A_LEFT=3\n\
BOOT_B_LEFT=3\n\
"));

	/* check rootfs.1 is marked primary (first in BOOT_ORDER, BOOT_B_LEFT reset to 3) */
	test_uboot_initialize_state("\
BOOT_ORDER=A B\n\
BOOT_A_LEFT=3\n\
BOOT_B_LEFT=1\n\
");
	g_assert_true(r_boot_set_primary(rootfs1, NULL));
	g_assert_true(test_uboot_post_state("\
BOOT_ORDER=B A\n\
BOOT_A_LEFT=3\n\
BOOT_B_LEFT=3\n\
"));

	/* check rootfs.1 is marked primary while rootfs.0 remains disabled (BOOT_A_LEFT remains 0)  */
	test_uboot_initialize_state("\
BOOT_ORDER=A B\n\
BOOT_A_LEFT=0\n\
BOOT_B_LEFT=0\n\
");
	g_assert_true(r_boot_set_primary(rootfs1, NULL));
	g_assert_true(test_uboot_post_state("\
BOOT_ORDER=B A\n\
BOOT_A_LEFT=0\n\
BOOT_B_LEFT=3\n\
"));
}

static void bootchooser_uboot_asymmetric(BootchooserFixture *fixture,
		gconstpointer user_data)
{
	RaucSlot *rootfs0 = NULL;
	RaucSlot *rescue = NULL;
	RaucSlot *primary = NULL;
	gboolean res;
	GError *ierror = NULL;

	const gchar *cfg_file = "\
[system]\n\
compatible=FooCorp Super BarBazzer\n\
bootloader=uboot\n\
mountprefix=/mnt/myrauc/\n\
\n\
[keyring]\n\
path=/etc/rauc/keyring/\n\
\n\
[slot.rescue.0]\n\
device=/dev/rescue-0\n\
type=raw\n\
bootname=R\n\
readonly=true\n\
\n\
[slot.rootfs.0]\n\
device=/dev/rootfs-0\n\
type=ext4\n\
bootname=A\n";

	gchar* pathname = write_tmp_file(fixture->tmpdir, "uboot_asymmetric.conf", cfg_file, NULL);
	g_assert_nonnull(pathname);

	g_clear_pointer(&r_context_conf()->configpath, g_free);
	r_context_conf()->configpath = pathname;
	r_context();

	rescue = find_config_slot_by_device(r_context()->config, "/dev/rescue-0");
	g_assert_nonnull(rescue);
	rootfs0 = find_config_slot_by_device(r_context()->config, "/dev/rootfs-0");
	g_assert_nonnull(rootfs0);

	/* check rootfs.0 is marked bad (not in BOOT_ORDER, BOOT_R_LEFT = 0) */
	test_uboot_initialize_state("\
BOOT_ORDER=R A\n\
BOOT_R_LEFT=3\n\
BOOT_A_LEFT=3\n\
");
	res = r_boot_set_state(rootfs0, FALSE, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);
	g_assert_true(test_uboot_post_state("\
BOOT_ORDER=R\n\
BOOT_R_LEFT=3\n\
BOOT_A_LEFT=0\n\
"));

	/* check rootfs.0 is marked primary (first in BOOT_ORDER) */
	test_uboot_initialize_state("\
BOOT_ORDER=R A\n\
BOOT_R_LEFT=3\n\
BOOT_A_LEFT=1\n\
");
	res = r_boot_set_primary(rootfs0, &ierror);
	g_assert_no_error(ierror);
	g_assert_true(res);
	g_assert_true(test_uboot_post_state("\
BOOT_ORDER=A R\n\
BOOT_R_LEFT=3\n\
BOOT_A_LEFT=3\n\
"));

	/* check rootfs.0 is considered primary (as rootfs.0 has BOOT_A_LEFT set to 0) */
	test_uboot_initialize_state("\
BOOT_ORDER=A R\n\
BOOT_R_LEFT=3\n\
BOOT_A_LEFT=3\n\
");
	primary = r_boot_get_primary(NULL);
	g_assert_nonnull(primary);
	g_assert(primary != rescue);
	g_assert(primary == rootfs0);
}

static void bootchooser_efi(BootchooserFixture *fixture,
		gconstpointer user_data)
{
	RaucSlot *slot;
	gboolean good;
	RaucSlot *primary = NULL;

	const gchar *cfg_file = "\
[system]\n\
compatible=FooCorp Super BarBazzer\n\
bootloader=efi\n\
mountprefix=/mnt/myrauc/\n\
\n\
[keyring]\n\
path=/etc/rauc/keyring/\n\
\n\
[slot.rescue.0]\n\
device=/dev/mtd4\n\
type=raw\n\
bootname=recover\n\
readonly=true\n\
\n\
[slot.rootfs.0]\n\
device=/dev/rootfs-0\n\
type=ext4\n\
bootname=system0\n\
\n\
[slot.rootfs.1]\n\
device=/dev/rootfs-1\n\
type=ext4\n\
bootname=system1\n";

	gchar* pathname = write_tmp_file(fixture->tmpdir, "efi.conf", cfg_file, NULL);
	g_assert_nonnull(pathname);

	g_clear_pointer(&r_context_conf()->configpath, g_free);
	r_context_conf()->configpath = pathname;
	r_context();

	slot = find_config_slot_by_device(r_context()->config, "/dev/rootfs-0");
	g_assert_nonnull(slot);

	g_assert_true(r_boot_get_state(slot, &good, NULL));
	g_assert_true(good);
	primary = r_boot_get_primary(NULL);
	g_assert_nonnull(primary);

	g_assert_true(r_boot_set_state(slot, FALSE, NULL));
	g_assert_true(r_boot_set_state(slot, TRUE, NULL));

	slot = find_config_slot_by_device(r_context()->config, "/dev/rootfs-1");
	g_assert_nonnull(slot);

	g_assert_true(r_boot_set_primary(slot, NULL));
}


int main(int argc, char *argv[])
{
	gchar *path;
	setlocale(LC_ALL, "C");

	path = g_strdup_printf("%s:%s", "test/bin", g_getenv("PATH"));
	g_setenv("PATH", path, TRUE);
	g_free(path);

	g_test_init(&argc, &argv, NULL);

	g_test_add("/bootchoser/barebox", BootchooserFixture, NULL,
			bootchooser_fixture_set_up, bootchooser_barebox,
			bootchooser_fixture_tear_down);

	g_test_add("/bootchoser/barebox-asymmetric", BootchooserFixture, NULL,
			bootchooser_fixture_set_up, bootchooser_barebox_asymmetric,
			bootchooser_fixture_tear_down);

	g_test_add("/bootchoser/grub", BootchooserFixture, NULL,
			bootchooser_fixture_set_up, bootchooser_grub,
			bootchooser_fixture_tear_down);

	g_test_add("/bootchoser/uboot", BootchooserFixture, NULL,
			bootchooser_fixture_set_up, bootchooser_uboot,
			bootchooser_fixture_tear_down);

	g_test_add("/bootchoser/uboot-asymmetric", BootchooserFixture, NULL,
			bootchooser_fixture_set_up, bootchooser_uboot_asymmetric,
			bootchooser_fixture_tear_down);

	g_test_add("/bootchoser/efi", BootchooserFixture, NULL,
			bootchooser_fixture_set_up, bootchooser_efi,
			bootchooser_fixture_tear_down);

	return g_test_run();
}
