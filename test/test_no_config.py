from common import NanoInitTestCase


class NoConfigTest(NanoInitTestCase):
    def test_no_config_stays_alive(self):
        rc = self.run_nanoinit_timeout("-v0", timeout=1)
        self.assertEqual(rc, 124)
