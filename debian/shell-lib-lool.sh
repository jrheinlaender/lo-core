update_lool_systemplate() {
        echo -n "Updating LibreOffice Online systemplate... "
        su lool --shell=/bin/sh -c 'loolwsd-systemplate-setup /var/lib/lool/systemplate /usr/lib/libreoffice >/dev/null 2>&1'
        echo "done."
}
