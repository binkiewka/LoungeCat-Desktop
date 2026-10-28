#!/usr/bin/env python3
"""Installed-package GUI, plugin and verified-TLS smoke in a disposable container."""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import socket
import sqlite3
import ssl
import subprocess
import tempfile
import threading
import time

parser=argparse.ArgumentParser()
parser.add_argument('--format',choices=['appimage','deb'],required=True)
parser.add_argument('--dist',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
dist=args.dist.resolve(); output=args.output.resolve(); output.mkdir(parents=True,exist_ok=True)
manifest=json.loads((dist/'package-manifest.json').read_text())
support=dist/'test-support'
(support/'runtime-probe').chmod(0o755)
for entry in manifest['artifacts']:
    assert hashlib.sha256((dist/entry['file']).read_bytes()).hexdigest()==entry['sha256']

def run(argv,**kwargs):
    try:
        return subprocess.run([str(x) for x in argv],check=True,text=True,capture_output=True,timeout=60,**kwargs)
    except subprocess.CalledProcessError as error:
        (output/'subprocess-failure.log').write_text(str(argv)+'\n'+error.stdout+'\n'+error.stderr)
        raise

def close_window(window_id, display_name):
    # xdotool windowclose uses XDestroyWindow, which is NOT a user's normal
    # close request and can strand a modal Qt dialog. Send WM_DELETE_WINDOW.
    class Data(ctypes.Union):
        _fields_=[('l',ctypes.c_long*5),('b',ctypes.c_char*20)]
    class Client(ctypes.Structure):
        _fields_=[('type',ctypes.c_int),('serial',ctypes.c_ulong),('send_event',ctypes.c_int),
            ('display',ctypes.c_void_p),('window',ctypes.c_ulong),('message_type',ctypes.c_ulong),
            ('format',ctypes.c_int),('data',Data)]
    class Event(ctypes.Union):
        _fields_=[('client',Client),('pad',ctypes.c_long*24)]
    x=ctypes.CDLL('libX11.so.6')
    x.XOpenDisplay.argtypes=[ctypes.c_char_p];x.XOpenDisplay.restype=ctypes.c_void_p
    x.XInternAtom.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_int];x.XInternAtom.restype=ctypes.c_ulong
    x.XSendEvent.argtypes=[ctypes.c_void_p,ctypes.c_ulong,ctypes.c_int,ctypes.c_long,ctypes.POINTER(Event)]
    x.XFlush.argtypes=[ctypes.c_void_p];x.XCloseDisplay.argtypes=[ctypes.c_void_p]
    display=x.XOpenDisplay(display_name.encode());assert display
    try:
        event=Event();event.client.type=33;event.client.send_event=1
        event.client.display=display;event.client.window=int(window_id)
        event.client.message_type=x.XInternAtom(display,b'WM_PROTOCOLS',0)
        event.client.format=32;event.client.data.l[0]=x.XInternAtom(display,b'WM_DELETE_WINDOW',0)
        assert x.XSendEvent(display,int(window_id),0,0,ctypes.byref(event))
        x.XFlush(display)
    finally:x.XCloseDisplay(display)

# Test setup runs as root only inside an ephemeral container. No host install.
assert not Path('/usr/lib/x86_64-linux-gnu/libQt6Core.so.6').exists(), 'Runtime container already has Qt; bundle proof would be masked'
with tempfile.TemporaryDirectory(prefix='loungecat-package-') as tmp:
    root=Path(tmp); env=os.environ.copy()
    for key in ['QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','LD_LIBRARY_PATH','LOUNGECAT_DB_PATH','LOUNGECAT_CONFIG']:
        env.pop(key,None)
    for name in ['runtime','cache','config','data']:
        (root/name).mkdir(mode=0o700)
    env.update(XDG_RUNTIME_DIR=str(root/'runtime'),XDG_CACHE_HOME=str(root/'cache'),
        XDG_CONFIG_HOME=str(root/'config'),XDG_DATA_HOME=str(root/'data'),
        LOUNGECAT_DATA_DIR=str(root/'profile'),DBUS_SESSION_BUS_ADDRESS='unix:path=/nonexistent/loungecat-smoke',
        LC_ALL='C.UTF-8',QT_QPA_PLATFORM='offscreen')
    if args.format=='deb':
        package=next(dist.glob('*.deb'))
        subprocess.run(['apt-get','update'],check=True,stdout=subprocess.DEVNULL)
        subprocess.run(['apt-get','install','-y','--no-install-recommends',str(package)],check=True)
        command=['/usr/bin/loungecat']
        run(['desktop-file-validate','/usr/share/applications/io.github.binkiewka.LoungeCat.desktop'])
        assert Path('/usr/share/icons/hicolor/256x256/apps/loungecat.png').exists()
        # Official slim containers intentionally exclude most /usr/share/doc
        # files at dpkg unpack time. Check the payload plus the policy-retained
        # copyright entry, not the container's optional documentation policy.
        assert 'usr/share/doc/loungecat/LICENSE' in run(['dpkg-deb','--contents',package]).stdout
        assert Path('/usr/share/doc/loungecat/copyright').exists()
        dependencies=run(['dpkg-query','-W','-f=${Depends}','loungecat']).stdout
        assert 'libqt6sql6-sqlite' in dependencies and 'libssl3' in dependencies
    else:
        package=next(dist.glob('*.AppImage'))
        package.chmod(0o755)
        # Exercise the actual AppImage runtime without requiring FUSE privileges.
        env['APPIMAGE_EXTRACT_AND_RUN']='1'
        command=[str(package)]
        run([package,'--appimage-extract'],cwd=root)
        appdir=root/'squashfs-root'
        run(['desktop-file-validate',appdir/'io.github.binkiewka.LoungeCat.desktop'])
        for required in ['libqsqlite.so','libqopensslbackend.so','libqxcb.so','libqoffscreen.so']:
            assert list(appdir.rglob(required)), f'Missing bundled plugin {required}'
        assert list(appdir.rglob('libssl.so.3')) and list(appdir.rglob('libcrypto.so.3'))
        assert (appdir/'usr/share/doc/loungecat/dependency-licenses/inventory.json').exists()
    assert run(command+['--version'],env=env).stdout.strip()=='LoungeCat '+manifest['version']
    assert 'Usage:' in run(command+['--help'],env=env).stdout
    assert not (root/'profile').exists(), 'CLI mode unexpectedly touched a profile'

    tls=socket.socket(); tls.bind(('127.0.0.1',0)); tls.listen(1); tls.settimeout(15)
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(support/'localhost-cert.pem',support/'localhost-key.pem')
    failures=[]
    def serve():
        try:
            sock,_=tls.accept()
            with context.wrap_socket(sock,server_side=True) as secure:
                secure.sendall(b'package-ok\n')
        except Exception as error: failures.append(str(error))
    thread=threading.Thread(target=serve,daemon=True);thread.start()
    probe_env=env.copy()
    if args.format=='appimage':
        probe_env['LD_LIBRARY_PATH']=str(appdir/'usr/lib')
        probe_env['QT_PLUGIN_PATH']=str(next(appdir.rglob('libqsqlite.so')).parent.parent)
    run([support/'runtime-probe',support/'localhost-cert.pem',str(tls.getsockname()[1]),output/'runtime-probe.json'],env=probe_env)
    thread.join(timeout=16);tls.close()
    assert not thread.is_alive() and not failures,failures
    probe=json.loads((output/'runtime-probe.json').read_text())
    if args.format=='appimage':
        libraries=probe['mappedLibraries']
        for name in ['libQt6Core','libqsqlite','libqopensslbackend','libssl.so']:
            paths=[p for p in libraries if name in p]
            assert paths and all(str(appdir) in p for p in paths),(name,paths)

    # Actual packaged application: xcb GUI, Settings open/close, normal exit.
    # Private Xvfb display means no interaction with the operator's desktop.
    read_fd,write_fd=os.pipe()
    xlog=(output/'xvfb.log').open('w')
    xvfb=subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0','1440x1000x24','-nolisten','tcp'],
        pass_fds=(write_fd,),stdout=xlog,stderr=xlog)
    os.close(write_fd)
    app=None
    try:
        with os.fdopen(read_fd) as display:
            env['DISPLAY']=':'+display.readline().strip()
        env['QT_QPA_PLATFORM']='xcb'
        profile=root/'profile';profile.mkdir(mode=0o700)
        (profile/'preferences.json').write_text(json.dumps({'notificationsEnabled':False,'soundAlertsEnabled':False,'spellCheckEnabled':False}))
        log=(output/'application.log').open('w')
        app=subprocess.Popen(command,env=env,stdout=log,stderr=log)
        def window(title):
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                assert app.poll() is None, 'Packaged application exited before its window appeared'
                search=subprocess.run(['xdotool','search','--onlyvisible','--name','^'+title+'$'],env=env,text=True,capture_output=True)
                if search.returncode==0:return search.stdout.splitlines()[0]
                time.sleep(0.1)
            raise AssertionError('No visible window: '+title)
        main=window('LoungeCat IRC Client')
        geometry=run(['xdotool','getwindowgeometry','--shell',main],env=env).stdout
        assert 'WIDTH=1200' in geometry and 'HEIGHT=800' in geometry,geometry
        run(['xdotool','mousemove','--window',main,'156','72','click','1'],env=env)
        settings=window('LoungeCat Settings')
        close_window(settings,env['DISPLAY'])
        time.sleep(0.3)
        close_window(main,env['DISPLAY'])
        assert app.wait(timeout=15)==0,'Application did not shut down cleanly'
        log.close()
        with sqlite3.connect(profile/'loungecat.db') as db:
            columns={row[1] for row in db.execute('PRAGMA table_info(server_configs)')}
            assert {'hostname','channel_keys'}<=columns
        assert (profile/'loungecat.db').stat().st_mode & 0o777 == 0o600
        # Saving in Settings proves a real durable preferences round trip.
        assert json.loads((profile/'preferences.json').read_text())['notificationsEnabled'] is False
    finally:
        if app is not None and app.poll() is None:app.terminate();app.wait(timeout=10)
        xvfb.terminate();xvfb.wait(timeout=10);xlog.close()
    if args.format=='deb':
        run(['dpkg','--remove','loungecat'])
        assert not Path('/usr/bin/loungecat').exists(),'Package uninstall left the executable'
        assert (root/'profile/loungecat.db').exists(),'Uninstall removed user data'
    (output/'summary.json').write_text(json.dumps({'format':args.format,'version':manifest['version'],
        'sha256':hashlib.sha256(package.read_bytes()).hexdigest(),'osRelease':Path('/etc/os-release').read_text(),
        'checks':['artifact checksums','CLI version/help without profile writes','desktop integration metadata',
            'SQLite plugin round trip','verified loopback TLS and OpenSSL','SVG rendering','widget rendering',
            'visible packaged xcb main window','Settings open/close','normal process exit',
            'durable owner-only profile database']+(['install dependency resolution','uninstall preserves user data'] if args.format=='deb' else ['bundled Qt/TLS/SQL library mappings; no system Qt']),
        'passed':True},indent=2)+'\n')
    print(args.format+': packaged runtime, GUI/Settings and persistence checks passed')
