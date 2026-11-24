COMPLEX pro Android - jak dostat APK
====================================

V balicku:
  main.cpp              hra (stejny zdroj jako na PC, na Androidu se prekladá s -DPLATFORM_ANDROID)
  AndroidManifest.xml   popis aplikace pro Android
  build_android.sh      skript, ktery z main.cpp udela complex.apk
  .github/workflows/build-apk.yml   navod pro GitHub, aby APK sestavil za tebe v cloudu


A) NEJJEDNODUSSI: nech APK sestavit GitHubem (zdarma, nic neinstalujes)
------------------------------------------------------------------------
1. Zaloz si ucet na github.com a vytvor novy repozitar (New repository), treba "complex".
2. Na strance repozitare dej "Add file" -> "Upload files" a nahraj SOUBORY:
      main.cpp, build_android.sh, AndroidManifest.xml
   a dej "Commit changes".
3. Dej "Add file" -> "Create new file". Do pole s nazvem napis PRESNE:
      .github/workflows/build-apk.yml
   (kdyz napises lomitka, GitHub sam vytvori slozky). Do obsahu vloz cely text ze souboru
   build-apk.yml z tohoto balicku a dej "Commit changes".
4. Otevri zalozku "Actions". Pod "Build APK" klikni "Run workflow" (po commitu se muze spustit sam).
5. Pocitej s 5-10 minutami. Az bude beh zeleny, otevri ho a dole v "Artifacts" stahni
   "complex-apk" - je to zip, uvnitr je complex.apk.
6. Kdyz beh skonci cervene, otevri ho, rozklikni cerveny krok a posli mi poslednich par radku chyby.


B) Instalace do telefonu
------------------------
- Prenes complex.apk do telefonu (kabel, Google Drive, e-mail...).
- Otevri ho v telefonu a povol "Instalovat z neznamych zdroju" pro aplikaci, ze ktere ho otevirate.
- Google Play Protect muze varovat: APK je podepsane jen ladicim klicem, ne Google Play klicem.
- Pozadavky: Android 10 a novejsi (API 29), 64bitovy telefon (arm64), OpenGL ES 2.0.
- Hra bezi na sirku.


C) Dotykove ovladani
--------------------
- Levy palec (levá polovina obrazovky): joystick - chodis.
- Pravy palec: tahem po obrazovce se rozhlizis.
- Tlacitka: KOP (kopat / kacet / bit kladivkem - drz), POUZIJ (pouzit vybranou vec),
  SKOK, BEH (prepinac sprintu), INV (inventar), E (pracovni stul), F (pit - drz).
- Hotbar dole se vybira klepnutim na slot. V inventari a u stolu klepni na recept.
- Po smrti klepni kamkoli pro znovuzrozeni.


D) Zname limity
---------------
- Pri prepnuti do jine aplikace a zpet muze hra prijit o grafiku (zname omezeni raylibu na Androidu) - restartuj ji.
- Na telefonu je nizsi dohled (6 chunku), mene travy a jednodussi shadery nez na PC.
- Texty jsou bez diakritiky. Zaznamy hry jdou do logcatu (ne do souboru).


E) Lokalni sestaveni (Linux nebo Windows s WSL)
-----------------------------------------------
  git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git
  (cd raylib/src && make PLATFORM=PLATFORM_ANDROID ANDROID_NDK=/cesta/k/ndk ANDROID_ARCH=arm64 ANDROID_API_VERSION=29)
  export ANDROID_SDK=/cesta/k/android-sdk
  export ANDROID_NDK=/cesta/k/ndk
  export RAYLIB_SRC=$PWD/raylib/src
  ./build_android.sh
Potrebujes Android SDK (build-tools), Android NDK a JDK. Vysledek: complex.apk
