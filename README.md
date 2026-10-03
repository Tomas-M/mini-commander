# mini-commander
Mini Commander is very simplified clone of Midnight Commander for Linux.
It even includes viewer and editor.

I would like to call myself an author, but that's not so easy.

The majority of the code, including this text itself, was initially written by
ChatGPT 4. Newer bugfixes were automatically detected and fixed by GPT-6 Astra.

ChatGPT doesn't generate code with an explicit license attached to it. The
code it provides in responses is intended for educational and informational
purposes, and users are free to use it as they see fit. So, the license
of Mini Commander is GNU GPL v3.

![mc](https://github.com/Tomas-M/mini-commander/assets/2259370/3ec02529-7e8d-4d74-9468-01f35b705b0f)

Tomas M
slax.org


Usage:

    make
    ./mc

    # Result of compilation is standalone 'mc' binary, it does not need anything else.
    # There is no make install because 'mc' would interfere with midnight commander.
    # So install it manually, for example copy ./mc to your path if you like
