#!/usr/bin/env python
#
# main.py
#
# Specific command-line utility for Mellanox platform
#

try:
    import sys
    import os
    import subprocess
    import click
    import syslog
    import time
except ImportError as e:
    raise ImportError("%s - required module not found" % str(e))

VERSION = '1.0'

SNIFFER_SYSLOG_IDENTIFIER = "sniffer"

# SDK sniffer env variable
ENV_VARIABLE_SX_SNIFFER = 'SX_SNIFFER_ENABLE'
ENV_VARIABLE_SX_SNIFFER_TARGET = 'SX_SNIFFER_TARGET'

# SDK sniffer file path and name
SDK_SNIFFER_TARGET_PATH = '/var/log/mellanox/sniffer/'
SDK_SNIFFER_FILENAME_PREFIX = 'sx_sdk_sniffer_'
SDK_SNIFFER_FILENAME_EXT = '.pcap'

# Supervisor config file path
TMP_SNIFFER_CONF_FILE = '/tmp/tmp.conf'
CONTAINER_NAME = 'syncd'
SNIFFER_CONF_FILE = '/etc/supervisor/conf.d/mlnx_sniffer.conf'
SNIFFER_CONF_FILE_IN_CONTAINER = CONTAINER_NAME + ':' + SNIFFER_CONF_FILE
# Command to restart swss service
COMMAND_RESTART_SWSS = 'systemctl restart swss.service'

# ========================== Syslog wrappers ==========================
def log_info(msg, syslog_identifier, also_print_to_console=False):
    syslog.openlog(syslog_identifier)
    syslog.syslog(syslog.LOG_INFO, msg)
    syslog.closelog()

    if also_print_to_console:
        print msg


def log_warning(msg, syslog_identifier, also_print_to_console=False):
    syslog.openlog(syslog_identifier)
    syslog.syslog(syslog.LOG_WARNING, msg)
    syslog.closelog()

    if also_print_to_console:
        print msg


def log_error(msg, syslog_identifier, also_print_to_console=False):
    syslog.openlog(syslog_identifier)
    syslog.syslog(syslog.LOG_ERR, msg)
    syslog.closelog()

    if also_print_to_console:
        print msg


# run command
def run_command(command, display_cmd=False, ignore_error=False):
    """Run bash command and print output to stdout
    """
    if display_cmd == True:
        click.echo(click.style("Running command: ", fg='cyan') + click.style(command, fg='green'))

    proc = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE)
    (out, err) = proc.communicate()

    if len(out) > 0:
        click.echo(out)

    if proc.returncode != 0 and not ignore_error:
        sys.exit(proc.returncode)


# generate sniffer target file name include a time stamp.
def sniffer_filename_generate(path, filename_prefix, filename_ext):
    time_stamp = time.strftime("%Y%m%d%H%M%S")
    filename = path + filename_prefix + time_stamp + filename_ext
    return filename


# write environment variable in local tmp file for sniffer
def env_variable_write(env_variable_string):
    conf_file = open(TMP_SNIFFER_CONF_FILE, 'a')
    if os.path.getsize(TMP_SNIFFER_CONF_FILE) == 0:
        conf_file.write('[program:syncd]\n')
    conf_file.write(env_variable_string)
    conf_file.close()


def env_variable_read(env_variable_name):
    conf_file = open(TMP_SNIFFER_CONF_FILE, 'r')
    for env_variable_string in conf_file:
        if env_variable_string.find(env_variable_name) >= 0:
            break
    else:
        env_variable_string = ''
    conf_file.close()
    return env_variable_string


def env_variable_delete(delete_line):
    conf_file = open(TMP_SNIFFER_CONF_FILE, 'r+')
    all_lines = conf_file.readlines()
    conf_file.seek(0)
    for line in all_lines:
        if line != delete_line:
            conf_file.write(line)
    conf_file.truncate()
    conf_file.close()


def conf_file_copy(src, dest):
    command = 'docker cp ' + src + ' ' + dest
    run_command(command)


def conf_file_receive():
    command = "docker exec {} bash -c 'touch {}'".format(CONTAINER_NAME, SNIFFER_CONF_FILE)
    run_command(command)
    conf_file_copy(SNIFFER_CONF_FILE_IN_CONTAINER, TMP_SNIFFER_CONF_FILE)


def config_file_send():
    conf_file_copy(TMP_SNIFFER_CONF_FILE, SNIFFER_CONF_FILE_IN_CONTAINER)


# set supervisor conf file for sniffer enable
def sniffer_env_variable_set(enable, env_variable_name, env_variable_string=""):
    ignore = False
    conf_file_receive()
    env_variable_exist_string = env_variable_read(env_variable_name)
    if env_variable_exist_string:
        if enable is True:
            print "sniffer is already enabled, do nothing"
            ignore = True
        else:
            env_variable_delete(env_variable_exist_string)
    else:
        if enable is True:
            env_variable_write(env_variable_string)
        else:
            print "sniffer is already disabled, do nothing"
            ignore = True

    if not ignore:
        config_file_send()

    command = 'rm -rf {}'.format(TMP_SNIFFER_CONF_FILE)
    run_command(command)

    return ignore


# restart the swss service with command 'service swss restart'
def restart_swss():
    try:
        run_command(COMMAND_RESTART_SWSS)
    except OSError as e:
        log_error("Not able to restart swss service, %s" % str(e), SNIFFER_SYSLOG_IDENTIFIER, True)
        return 1
    return 0


# ==================== CLI commands and groups ====================

# Callback for confirmation prompt. Aborts if user enters "n"
def _abort_if_false(ctx, param, value):
    if not value:
        ctx.abort()


# 'mlnx' group
@click.group()
def mlnx():
    """ Mellanox platform configuration tasks """
    pass


# 'sniffer' group
@mlnx.group()
def sniffer():
    """ Utility for managing Mellanox SDK/PRM sniffer """
    pass


# 'sdk' subgroup
@sniffer.group()
def sdk():
    """SDK Sniffer - Command Line to enable/disable SDK sniffer"""
    pass


@sdk.command()
@click.option('-y', '--yes', is_flag=True, callback=_abort_if_false, expose_value=False,
              prompt='Swss service will be restarted, continue?')
def enable():
    """Enable SDK Sniffer"""
    print "Enabling SDK sniffer"
    sdk_sniffer_enable()
    print "Note: the sniffer file may exhaust the space on /var/log, please disable it when you are done with this sniffering."


@sdk.command()
@click.option('-y', '--yes', is_flag=True, callback=_abort_if_false, expose_value=False,
              prompt='Swss service will be restarted, continue?')
def disable():
    """Disable SDK Sniffer"""
    print "Disabling SDK sniffer"
    sdk_sniffer_disable()


def sdk_sniffer_enable():
    """Enable SDK Sniffer"""
    sdk_sniffer_filename = sniffer_filename_generate(SDK_SNIFFER_TARGET_PATH,
                                                     SDK_SNIFFER_FILENAME_PREFIX,
                                                     SDK_SNIFFER_FILENAME_EXT)
    sdk_sniffer_env_variable_dict = {ENV_VARIABLE_SX_SNIFFER: "1" + ",",
                                     ENV_VARIABLE_SX_SNIFFER_TARGET: sdk_sniffer_filename}
    sdk_sniffer_env_variable_string = "environment="

    for env_variable_name, env_variable_value in sdk_sniffer_env_variable_dict.items():
        sdk_sniffer_env_variable_string += (env_variable_name + "=" + env_variable_value)

    sdk_sniffer_env_variable_string += "\n"

    ignore = sniffer_env_variable_set(enable=True, env_variable_name=ENV_VARIABLE_SX_SNIFFER,
                                      env_variable_string=sdk_sniffer_env_variable_string)
    if not ignore:
        err = restart_swss()
        if err is not 0:
            return
        print 'SDK sniffer is Enabled, recording file is %s.' % sdk_sniffer_filename
    else:
        pass


def sdk_sniffer_disable():
    """Disable SDK Sniffer"""

    ignore = sniffer_env_variable_set(enable=False, env_variable_name=ENV_VARIABLE_SX_SNIFFER)
    if not ignore:
        err = restart_swss()
        if err is not 0:
            return
        print "SDK sniffer is Disabled."
    else:
        pass


# place holders for 'sniff prm enable/disable' and 'sniffer all enable/disable'
# @sniffer.command()
# @click.argument('option', type=click.Choice(["enable", "disable"]))
# def prf():
#     pass
#
#
# @sniffer.command()
# @click.argument('option', type=click.Choice(["enable", "disable"]))
# def all():
#     pass


if __name__ == '__main__':
    sniffer()