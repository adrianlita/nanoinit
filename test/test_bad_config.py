from common import ROOT_DIR, NanoInitTestCase


class BadConfigTest(NanoInitTestCase):
    def test_bad_config_falls_back_to_zero_config(self):
        rc = self.run_nanoinit_timeout("-c", ROOT_DIR / "test" / "test-config.json", "-v0", timeout=1)
        self.assertEqual(rc, 124)
