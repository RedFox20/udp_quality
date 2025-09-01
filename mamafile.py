import mama
import os
import mama.util


# Explore Mama docs at https://github.com/RedFox20/Mama
class udp_quality(mama.BuildTarget):

    local_workspace = 'packages'

    def init(self):
        self.version = '1.0'

    def settings(self):
        self.set_artifactory_ftp('krattifacts.codefox.ee', auth='store')
        self.config.prefer_gcc('udp_quality')
        if self.mips:
            self.config.set_mips_toolchain('mipsel')


    def dependencies(self):
        self.add_git('ReCpp', 'https://github.com/RedFox20/ReCpp.git')

    def package(self):
        self.export_libs('bin', ['udp_quality', 'udp_quality.exe'])
        self.no_export_includes()

    def start(self, args):
        self.run_program(self.build_dir(), f'bin/udp_quality {args}')

