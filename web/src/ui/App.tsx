import { useEffect, useRef } from 'react';
import { DEVICE_NEEDS_PERMISSION } from '../config';
import { deviceViewChosen, restoreCardScope, setDeviceView } from '../card/session';
import { useArtPackSync } from '../jcard/active';
import { loadDrafts } from '../playlists/drafts';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { countMedia } from '../storage/media';
import { restoreCoversFromDevice } from './cardWriter';
import { ArtistArea } from './areas/Artist';
import { MediaArea } from './areas/Media';
import { PlayerArea } from './areas/Player';
import { PlaylistEditor } from './areas/PlaylistEditor';
import { PlaylistsArea } from './areas/Playlists';
import { PrepareArea } from './areas/Prepare';
import { SettingsArea } from './areas/Settings';
import { VideoArea } from './areas/Video';
import { navigate, routeHref, useRoute } from './router';
import { NotFound, Shell } from './Shell';
import { PlaybackProvider } from './playback';
import { tryLocalConnect } from '../device/device';
import { AnalysisProvider } from './state/analysis';
import { OfflineNotice } from './OfflineNotice';
import { UpdateBanner } from './UpdateBanner';
import { LocalNetworkGate } from './LocalNetworkGate';
import { prepareOffline } from './prefetchStore';

/**
 * The App, behind the Local Network Access gate.
 *
 * Nothing here can work without the player, and from the shipping (public) origin the player
 * is only reachable while Chrome's `local-network` permission is granted, so the gate decides
 * whether this tree mounts at all. It sits above the providers on purpose: no analysis
 * worker, no playback owner and no poll starts before there is a player to point them at.
 *
 * A development build is served from a LAN address, which the permission never gates, so it
 * mounts the same tree without the gate (k7-app.md A3; ADR 0082). Both builds then reach the
 * player the one way: `tryLocalConnect()` below.
 */
export function App() {
  if (!DEVICE_NEEDS_PERMISSION) return <AppContent />;
  return (
    <LocalNetworkGate>
      <AppContent />
    </LocalNetworkGate>
  );
}

/**
 * The shell: four areas and the routes between them (ADR 0052).
 *
 * The Card state chooses the first area and nothing after that: with a card in the
 * player the page opens on Player, without one on Prepare. Every later change is the
 * user's -- what S3 protected is structural instead, because the Prepare area stays
 * mounted while another route is shown, so navigating never destroys a running Queue.
 */
function AppContent() {
  /* The active user pack: cached first, then the player's answer, so a coverless album
   * draws before the first fetch and follows a pack that is installed or reset while the
   * App is open (art-packs.md §4/§6). */
  useArtPackSync();
  const route = useRoute();
  const { status, reachable } = useDeviceStatus();
  /* The first time the player answers with a card, and only if this browser has never
   * chosen a view, show the player's card: it is what most people come here for, and the
   * choice is remembered once it is made. */
  const autoViewed = useRef(false);
  useEffect(() => {
    if (autoViewed.current || !reachable || status?.card !== true || status.mark === null) return;
    autoViewed.current = true;
    if (!deviceViewChosen()) setDeviceView(true);
  }, [reachable, status]);
  const landed = useRef(false);

  useEffect(() => {
    void loadDrafts();
  }, []);

  /* The card this browser last worked on, when the scope was not recorded: everything the
   * Media and Playlists areas show is keyed by it, and the pick that would name it again
   * cannot happen while the card is in the player. */
  useEffect(() => {
    void restoreCardScope();
  }, []);

  /* The player is not a place you go, it is a fact about the page: try the direct path on
   * load. The first attempt raises the Local Network Access prompt; after it is granted —
   * or if it was granted before — this connects with nothing to click and no window. */
  useEffect(() => {
    void tryLocalConnect();
  }, []);

  /* The IA Agent's old address has no page any more; land on the list of lists and rewrite
   * the address, so a reload does not keep asking for a place that is gone (ADR 0093). */
  useEffect(() => {
    if (window.location.hash === '#/playlists/ask') {
      navigate(routeHref({ name: 'playlists' }), { replace: true });
    }
  }, [route]);

  /* O4: the 41 MB is downloaded for people who installed the app, and for nobody else. */
  useEffect(() => {
    if (typeof window.matchMedia !== 'function') return;
    if (!window.matchMedia('(display-mode: standalone)').matches) return;
    void prepareOffline();
  }, []);

  useEffect(() => {
    if (landed.current || !status) return;
    /* Nothing chosen yet: the address bar is empty. It is filled with the Player rather than
     * an area the card state picks - a reload belongs where the music is, and the card the
     * player holds is selected on its own. */
    if (window.location.hash === '') {
      landed.current = true;
      navigate('#/player', { replace: true });
    }
  }, [status]);

  /* A card in the player is the one way to get its preprocessed covers while it is not in
   * the computer: when the device reports a card and this browser has no rows for it, pull
   * the archive once. It runs in the background, so opening Media later already has them. */
  const restoredCovers = useRef(new Set<string>());
  useEffect(() => {
    if (!reachable || !status?.card || !status.mark) return;
    const scope = `mark:${status.mark}`;
    if (restoredCovers.current.has(scope)) return;
    restoredCovers.current.add(scope);
    void (async () => {
      try {
        if ((await countMedia(scope)) > 0) return;
        await restoreCoversFromDevice();
      } catch {
        /* A device that cannot stream its covers is not a reason to break the page. */
      }
    })();
  }, [reachable, status]);

  return (
    <AnalysisProvider>
      <PlaybackProvider>
        <Shell route={route}>
          {/* Mounted always, hidden when it is not the route: a queue in flight outlives a
              visit to Media. */}
          <div hidden={route.name !== 'prepare'} className="min-h-0">
            <PrepareArea />
          </div>

          {route.name === 'playlists' && <PlaylistsArea />}
          {route.name === 'playlist' && <PlaylistEditor id={route.id} />}
          {/* Media is one area with two views: the route chooses which from the same slot, so
              the cache is read once while a reader walks between the shelf and the folders.
              An album route turns the case on the shelf; a drive route opens its folder. */}
          {(route.name === 'media' || route.name === 'album' || route.name === 'drive') && (
            <MediaArea
              album={route.name === 'album' ? route.album : undefined}
              drive={route.name === 'drive' ? route.path : undefined}
            />
          )}
          {route.name === 'artist' && <ArtistArea artist={route.artist} />}
          {route.name === 'video' && <VideoArea id={route.id} />}
          {route.name === 'player' && <PlayerArea />}
          {route.name === 'settings' && <SettingsArea />}
          {route.name === 'not-found' && <NotFound path={route.path} />}
        </Shell>
        <UpdateBanner />
        <OfflineNotice />
      </PlaybackProvider>
    </AnalysisProvider>
  );
}

