import { useEffect, useRef } from 'react';
import { restoreCardScope } from '../card/session';
import { loadDrafts } from '../playlists/drafts';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { AlbumArea } from './areas/Album';
import { ArtistArea } from './areas/Artist';
import { AskArea } from './areas/Ask';
import { MediaArea } from './areas/Media';
import { PlayerArea } from './areas/Player';
import { PlaylistEditor } from './areas/PlaylistEditor';
import { PlaylistsArea } from './areas/Playlists';
import { ProcessArea } from './areas/Process';
import { SettingsArea } from './areas/Settings';
import { VideoArea } from './areas/Video';
import { navigate, useRoute } from './router';
import { NotFound, Shell } from './Shell';
import { tryLocalConnect } from '../device/device';
import { AnalysisProvider } from './state/analysis';
import { OfflineNotice } from './OfflineNotice';
import { UpdateBanner } from './UpdateBanner';
import { prepareOffline } from './prefetchStore';

/**
 * The shell: four areas and the routes between them (ADR 0052).
 *
 * The Card state chooses the first area and nothing after that: with a card in the
 * player the page opens on Player, without one on Process. Every later change is the
 * user's -- what S3 protected is structural instead, because the Process area stays
 * mounted while another route is shown, so navigating never destroys a running Queue.
 */
export function App() {
  const route = useRoute();
  const { status } = useDeviceStatus();
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

  /* O4: the 41 MB is downloaded for people who installed the app, and for nobody else. */
  useEffect(() => {
    if (typeof window.matchMedia !== 'function') return;
    if (!window.matchMedia('(display-mode: standalone)').matches) return;
    void prepareOffline();
  }, []);

  useEffect(() => {
    if (landed.current || !status) return;
    // Nothing chosen yet: the address bar is empty, and this is the only moment the card
    // state picks an area.
    if (window.location.hash === '') {
      landed.current = true;
      navigate(status.card ? '#/player' : '#/process', { replace: true });
    }
  }, [status]);

  return (
    <AnalysisProvider>
      <Shell route={route}>
        {/* Mounted always, hidden when it is not the route: a queue in flight outlives a
            visit to Media. */}
        <div hidden={route.name !== 'process'} className="min-h-0">
          <ProcessArea />
        </div>

        {route.name === 'playlists' && <PlaylistsArea />}
        {route.name === 'playlist' && <PlaylistEditor id={route.id} />}
        {route.name === 'ask' && <AskArea />}
        {route.name === 'media' && <MediaArea />}
        {route.name === 'album' && <AlbumArea artist={route.artist} album={route.album} />}
        {route.name === 'artist' && <ArtistArea artist={route.artist} />}
        {route.name === 'video' && <VideoArea id={route.id} />}
        {route.name === 'player' && <PlayerArea />}
        {route.name === 'settings' && <SettingsArea />}
        {route.name === 'not-found' && <NotFound path={route.path} />}
      </Shell>
      <UpdateBanner />
      <OfflineNotice />
    </AnalysisProvider>
  );
}

