AFTER ENSURING COOKIES ARE CORRECT RUN THIS TO DOWNLOAD FROM YOUTUBE LIKE MUSIC (CHANGE ITEM COUNT FOR HOW FAR YOU WANT TO GO)

uvx "yt-dlp[default]" --js-runtimes "deno:C:\Users\evant\AppData\Local\Microsoft\WinGet\Packages\DenoLand.Deno_Microsoft.Winget.Source_8wekyb3d8bbwe\deno.exe"
--cookies youtube_cookies.txt -f 140 --embed-metadata --embed-thumbnail --playlist-items 1-103 
-o "C:\Users\evant\Desktop\MP3 Project\ytmusic_downloader\YouTube Music\%(artist)s\%(album)s\%(title)s.%(ext)s" "https://music.youtube.com/playlist?list=LM"
