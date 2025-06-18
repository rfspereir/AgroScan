import { ComponentFixture, TestBed } from '@angular/core/testing';

import { Permissoes } from './permissoes';

describe('Permissoes', () => {
  let component: Permissoes;
  let fixture: ComponentFixture<Permissoes>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [Permissoes]
    })
    .compileComponents();

    fixture = TestBed.createComponent(Permissoes);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
